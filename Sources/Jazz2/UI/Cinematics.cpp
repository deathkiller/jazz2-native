#include "Cinematics.h"
#include "../PreferencesCache.h"
#include "../ContentFileTypes.h"
#include "../VideoFormat.h"
#include "../PlayerAction.h"
#include "../Input/ControlScheme.h"

#include "../../nCine/Application.h"
#include "../../nCine/ServiceLocator.h"
#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Input/JoyMapping.h"
#include "../../nCine/Audio/AudioBufferPlayer.h"
#include "../../nCine/Base/FrameTimer.h"

#include <Base/Memory.h>
#include <Containers/StringConcatenable.h>
#include <IO/Compression/DeflateStream.h>

#if defined(DEATH_TARGET_DREAMCAST)
#	include <dc/sq.h>
#endif

using namespace Death::Memory;
using namespace Jazz2::Input;

namespace Jazz2::UI
{
	namespace
	{
		/**
			@brief Resolves a run of palette indices into RGB565

			Four indices arrive in one 32-bit load and two colors leave in one 32-bit store, which is the fewest
			memory operations this can be done in - the straightforward byte-at-a-time loop is what the whole
			frame conversion costs most of its time in. Anything the wide path cannot cover (an odd length, or a
			buffer that is not 32-bit aligned) is done one index at a time.
		*/
		DEATH_ALWAYS_INLINE void ConvertIndicesTo565(const std::uint8_t* DEATH_RESTRICT src,
			std::uint16_t* DEATH_RESTRICT dst, const std::uint16_t* DEATH_RESTRICT palette, std::uint32_t count)
		{
			std::uint32_t i = 0;
#if !defined(DEATH_TARGET_BIG_ENDIAN)
			if (((reinterpret_cast<std::uintptr_t>(src) | reinterpret_cast<std::uintptr_t>(dst)) & 3) == 0) {
				const std::uint32_t* DEATH_RESTRICT src32 = reinterpret_cast<const std::uint32_t*>(src);
				std::uint32_t* DEATH_RESTRICT dst32 = reinterpret_cast<std::uint32_t*>(dst);
				const std::uint32_t quads = count / 4;
				for (std::uint32_t q = 0; q < quads; q++) {
					const std::uint32_t four = src32[q];
					const std::uint32_t c0 = palette[four & 0xFF];
					const std::uint32_t c1 = palette[(four >> 8) & 0xFF];
					const std::uint32_t c2 = palette[(four >> 16) & 0xFF];
					const std::uint32_t c3 = palette[four >> 24];
					dst32[(q * 2) + 0] = c0 | (c1 << 16);
					dst32[(q * 2) + 1] = c2 | (c3 << 16);
				}
				i = quads * 4;
			}
#endif
			for (; i < count; i++) {
				dst[i] = palette[src[i]];
			}
		}

		/**
			@brief Reorders a palette read from the video file into the engine's numeric RGBA convention

			The file stores an entry as the byte sequence R, G, B, X, but everything downstream treats an
			entry as a `std::uint32_t` VALUE with red in the lowest byte - the opaque-alpha override in
			`ApplyPaletteAndUpload()` (`| 0xFF000000`), the RGB565 packing, and the console backends' TLUT
			and bake conversions (`GxDevice::AcquireTlutForRow()`, `GxTexture::EnsureBakedRgba()`) all
			extract channels that way, matching what `nCine::Color` guarantees on both endiannesses by
			reordering its members. A raw byte copy only produces that value on little-endian hosts; a
			big-endian load (Wii, GameCube) reverses the channels - the alpha override then lands on RED
			and the TLUT reads R from the padding byte - so the value is byte-swapped back once here,
			right where the palette leaves the file.
		*/
		DEATH_ALWAYS_INLINE void NormalizePaletteByteOrder(std::uint32_t* palette, std::size_t count)
		{
#if defined(DEATH_TARGET_BIG_ENDIAN)
			for (std::size_t i = 0; i < count; i++) {
				palette[i] = AsLE(palette[i]);
			}
#else
			// Little-endian hosts already read the file bytes into the expected value layout
			static_cast<void>(palette);
			static_cast<void>(count);
#endif
		}
	}
	Cinematics::Cinematics(IRootController* root, StringView path, Function<bool(IRootController*, bool)>&& callback)
		: _root(root), _callback(std::move(callback)), _frameDelay(0.0f), _frameProgress(0.0f), _framesLeft(0), _frameIndex(0),
			_videoDownscale(1), _textureWidth(0), _textureHeight(0), _textureIndex(0),
			_pressedKeys(ValueInit, (std::size_t)Keys::Count), _pressedActions(0), _decodingFailed(false),
			_nativeFormat(false), _convertTo565(false), _paletteDirty(true),
			_paletteTextureDirty(false)
	{
		Initialize(path);
	}

	Cinematics::~Cinematics()
	{
		_canvas->setParent(nullptr);
	}

	Vector2i Cinematics::GetViewSize() const
	{
		return _upscalePass.GetViewSize();
	}

	void Cinematics::OnBeginFrame()
	{
		// The frame timer clamps GetTimeMult() to keep gameplay stable on slow frames, but the video has
		// to track real time - the music plays in real time, and on platforms that can't render 60 FPS
		// the clamp would stretch the video far beyond its runtime
		float timeMult = theApplication().GetFrameTimer().GetLastFrameDuration() / FrameTimer::SecondsPerFrame;

		if (_framesLeft <= 0) {
			if (_callback && _callback(_root, _frameDelay != 0.0f)) {
				_callback = nullptr;
			}
			return;
		}

		if (_frameIndex == 0) {
			// The first measured duration still includes the loading before the video started - it would
			// otherwise be skipped over as a giant catch-up backlog
			timeMult = std::min(timeMult, _frameDelay);
		}

		_frameProgress += timeMult;

		// Frames are delta-encoded against their predecessor, so a frame cannot be skipped - it always has
		// to be decoded, and decoding is nearly the whole cost (applying the palette and uploading the
		// texture is a few percent on top).
#if defined(DEATH_TARGET_DREAMCAST) || defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE)
		// On a machine that decodes slower than the video's frame rate, catching up buys nothing: it hides
		// all but the last of the decoded frames while making the stall worse. At most one frame is decoded
		// per rendered frame, every frame is shown, and the picture may fall behind the music.
		constexpr std::int32_t MaxDecodesPerFrame = 1;
#else
		// Decoding is cheap here, but a single long frame (a window drag, a driver compiling shaders) would
		// with a one-decode cap delay the video against the already-playing music for the rest of playback.
		// A small bounded catch-up recovers from such a hitch within a few frames, while a sustained decode
		// backlog still cannot snowball into an unbounded burst.
		constexpr std::int32_t MaxDecodesPerFrame = 3;
#endif
		if (_frameProgress > _frameDelay * MaxDecodesPerFrame) {
			_frameProgress = _frameDelay * MaxDecodesPerFrame;
		}

		for (std::int32_t i = 0; i < MaxDecodesPerFrame && _framesLeft > 0 && _frameProgress >= _frameDelay; i++) {
			_frameProgress -= _frameDelay;
			_framesLeft--;
			PrepareNextFrame();
			if DEATH_UNLIKELY(_decodingFailed) {
				// The compressed streams ended prematurely (truncated or corrupted file), finish playback
				// instead of looping on a dead stream
				_framesLeft = 0;
			}
		}

		UpdatePressedActions();

		if ((_pressedActions & ((1 << (std::int32_t)PlayerAction::Fire) | (1 << (16 + (std::int32_t)PlayerAction::Fire)))) == (1 << (std::int32_t)PlayerAction::Fire)) {
			if (_callback && _callback(_root, false)) {
				_callback = nullptr;
				_framesLeft = 0;
			}
		}
	}

	void Cinematics::OnInitializeViewport(std::int32_t width, std::int32_t height)
	{
		constexpr float defaultRatio = (float)DefaultWidth / DefaultHeight;
		float currentRatio = (float)width / height;

		std::int32_t w, h;
		if (currentRatio > defaultRatio) {
			w = std::min(DefaultWidth, width);
			h = (std::int32_t)roundf(w / currentRatio);
		} else if (currentRatio < defaultRatio) {
			h = std::min(DefaultHeight, height);
			w = (std::int32_t)roundf(h * currentRatio);
		} else {
			w = std::min(DefaultWidth, width);
			h = std::min(DefaultHeight, height);
		}

		_upscalePass.Initialize(w, h, width, height);

		// Viewports must be registered in reverse order
		_upscalePass.Register();

		_canvas->setParent(_upscalePass.GetNode());
	}

	void Cinematics::OnKeyPressed(const KeyboardEvent& event)
	{
		_pressedKeys.set((std::size_t)event.sym);
	}

	void Cinematics::OnKeyReleased(const KeyboardEvent& event)
	{
		_pressedKeys.reset((std::size_t)event.sym);
	}

	void Cinematics::OnTouchEvent(const TouchEvent& event)
	{
		if (event.type == TouchEventType::Down) {
			if (_callback && _callback(_root, false)) {
				_callback = nullptr;
				_framesLeft = 0;
			}
		}
	}

	void Cinematics::Initialize(StringView path)
	{
		theApplication().GetGfxDevice().setWindowTitle("Jazz² Resurrection"_s);

		_canvas = std::make_unique<CinematicsCanvas>(this);

		auto& resolver = ContentResolver::Get();

		if (!LoadCinematicsFromFile(path)) {
			_framesLeft = 0;
			return;
		}

#if defined(WITH_AUDIO) && defined(WITH_OPENMPT)
		_music = resolver.GetMusic(String(path + ".j2b"_s));
		if (_music != nullptr) {
			_music->setGain(PreferencesCache::MasterVolume * PreferencesCache::MusicVolume);
			_music->setSourceRelative(true);
			_music->play();
		}
#endif

		// Mark Fire button as already pressed to avoid some issues
		_pressedActions = (1 << (std::int32_t)PlayerAction::Fire) | (1 << ((std::int32_t)PlayerAction::Fire + 16));
	}

	bool Cinematics::LoadCinematicsFromFile(StringView path)
	{
		// Try "Content" directory first, then "Source" directory
		auto& resolver = ContentResolver::Get();
		auto s = resolver.OpenContentFile(fs::CombinePath("Cinematics"_s, String(path + ".j2v"_s)), 64 * 1024);
		if (!s->IsValid()) {
			if (auto alternativePath = fs::FindPathCaseInsensitive(fs::CombinePath(resolver.GetSourcePath(), String(path + ".j2v"_s)))) {
				s = fs::Open(alternativePath, FileAccess::Read, 64 * 1024);
			}
		}

		if (!s->IsValid()) {
			LOGW("Cannot load \"{}.j2v\" - Cinematics skipped", path);
			return false;
		}
		
		DEATH_ASSERT(s->GetSize() > 32 && s->GetSize() < 64 * 1024 * 1024,
			("Cannot load \"{}.j2v\" - Unexpected file size", path), false);

		// Both formats are accepted and told apart by their signature: the original one starts with
		// "CineFeed", the game's own with the signature every file it writes uses (see VideoFormat)
		std::uint8_t internalBuffer[16];
		s->Read(internalBuffer, 16);
		if (strncmp((const char*)internalBuffer, "CineFeed", sizeof("CineFeed") - 1) == 0) {
			return LoadLegacyVideo(std::move(s), path);
		}

		std::uint64_t signature = 0;
		std::memcpy(&signature, internalBuffer, sizeof(signature));
		if (AsLE(signature) == VideoFormat::Signature && internalBuffer[8] == ContentFileType::Video) {
			s->Seek(9, SeekOrigin::Begin);
			return LoadNativeVideo(std::move(s), path);
		}

		LOGE("Cannot load \"{}.j2v\" - Invalid signature", path);
		return false;
	}

	void Cinematics::SetupFrameTextureSize(std::uint32_t downscale)
	{
		_videoDownscale = (downscale > 0 ? downscale : 1);

		// A frame that doesn't fit a single hardware texture is halved until it does. Backends that page
		// an oversized image (the PSP's GE addresses at most 512 texels per axis) select the page per
		// primitive, and one quad covering the whole frame samples only the page its left edge is in - the
		// overhang then repeats that page's last column, which showed the right 128 columns of a 640x480
		// video as a horizontal smear across the last ~100 px of the panel. Halving is also the trade the
		// Dreamcast already makes above for bandwidth, and every platform whose limit this hits is one
		// that would struggle with the full-resolution upload anyway.
		const std::int32_t maxTextureSize = theServiceLocator().GetRhiCapabilities()
			.GetValue(RHI::IRhiCapabilities::IntValues::MaxTextureSize);
		if (maxTextureSize > 0) {
			const std::uint32_t limit = (std::uint32_t)maxTextureSize;
			while ((_width / _videoDownscale) > limit || (_height / _videoDownscale) > limit) {
				_videoDownscale *= 2;
			}
			if (_videoDownscale != downscale) {
				LOGI("Video {}x{} exceeds the maximum texture size {}, playing it downscaled {}x",
					_width, _height, maxTextureSize, _videoDownscale);
			}
		}

		_textureWidth = _width / _videoDownscale;
		_textureHeight = _height / _videoDownscale;
	}

	bool Cinematics::LoadLegacyVideo(std::unique_ptr<Stream>&& s, StringView path)
	{
		_nativeFormat = false;

		_width = s->ReadValueAsLE<std::uint32_t>();
		_height = s->ReadValueAsLE<std::uint32_t>();
		s->Seek(2, SeekOrigin::Current); // Bits per pixel
		_frameDelay = s->ReadValueAsLE<std::uint16_t>() / (FrameTimer::SecondsPerFrame * 1000); // Delay in milliseconds
		_framesLeft = s->ReadValueAsLE<std::uint32_t>();
		s->Seek(20, SeekOrigin::Current);

#if defined(DEATH_TARGET_DREAMCAST)
		// The 200 MHz SH-4 cannot move the ~4 MB per frame that a full-resolution upload needs (palette
		// apply, texture copy, then the conversion and twiddling into video memory). Videos that were already
		// downscaled offline by the AssetPacker are played as they are - halving them twice would throw away
		// detail the platform can afford to show.
		SetupFrameTextureSize(_width > 400 ? 2 : 1);
#else
		SetupFrameTextureSize(1);
#endif

		// Frames are palette indices. Where the hardware can sample those directly they are uploaded as they
		// are; where a paletted texture would have to be twiddled every frame they are converted instead (see
		// _convertTo565).
#if defined(DEATH_TARGET_DREAMCAST)
		_convertTo565 = true;
#else
		_convertTo565 = false;
#endif
		const Texture::Format textureFormat = (_convertTo565 ? Texture::Format::RGB565 : Texture::Format::R8);
		_textures[0] = std::make_unique<Texture>("Cinematics", textureFormat, _textureWidth, _textureHeight);
		_textures[1] = std::make_unique<Texture>("Cinematics", textureFormat, _textureWidth, _textureHeight);
		_textures[0]->SetMinFiltering(SamplerFilter::Nearest);
		_textures[0]->SetMagFiltering(SamplerFilter::Nearest);
		_textures[1]->SetMinFiltering(SamplerFilter::Nearest);
		_textures[1]->SetMagFiltering(SamplerFilter::Nearest);
		_paletteTexture = std::make_unique<Texture>("CinematicsPalette", Texture::Format::RGBA8, 256, 1);
		_paletteTexture->SetMinFiltering(SamplerFilter::Nearest);
		_paletteTexture->SetMagFiltering(SamplerFilter::Nearest);
		_paletteTexture->SetWrap(SamplerWrapping::ClampToEdge);
		// The entries reach the texture as raw uint32 values (see NormalizePaletteByteOrder above), so the
		// texel channel order follows the host's endianness exactly as it does for the shared palette
		ContentResolver::ConfigurePaletteTextureChannels(*_paletteTexture);
		_paletteDirty = true;
		_textureIndex = 0;
		_buffer = std::make_unique<std::uint8_t[]>(_width * _height);
		_lastBuffer = std::make_unique<std::uint8_t[]>(_width * _height);
		if (_videoDownscale > 1) {
			_indexedFrame = std::make_unique<std::uint8_t[]>(_textureWidth * _textureHeight);
		}
		if (_convertTo565) {
			_frameRow = std::make_unique<std::uint16_t[]>(_textureWidth);
		}

		// Build the chunk index of the 4 interleaved compressed streams - only chunk positions are kept
		// and the data is read from the file on demand while the video plays, so the whole video never
		// has to be buffered into memory (required on memory-constrained platforms)
		SmallVector<Pair<std::int64_t, std::int32_t>, 0> chunks[arraySize(&Cinematics::_decompressedStreams)];
		std::int64_t totalOffset = s->GetPosition();
		const std::int64_t fileSize = s->GetSize();

		while (totalOffset < fileSize) {
			for (std::int32_t i = 0; i < std::int32_t(arraySize(&Cinematics::_decompressedStreams)); i++) {
				std::int32_t bytesLeft = s->ReadValueAsLE<std::int32_t>();
				totalOffset += 4;
				chunks[i].push_back(Pair(totalOffset, bytesLeft));
				totalOffset += bytesLeft;
				s->Seek(bytesLeft, SeekOrigin::Current);
			}
		}

		// All four streams read the file through one shared read-ahead buffer (see FileWindow), which
		// starts where the first chunk does
		_fileWindow.Initialize(s.get(), (chunks[0].empty() ? 0 : chunks[0][0].first()));

		for (std::int32_t i = 0; i < std::int32_t(arraySize(&Cinematics::_decompressedStreams)); i++) {
			// Skip first two bytes (zlib header 0x78 0xDA)
			_compressedStreams[i].Initialize(&_fileWindow, std::move(chunks[i]), 2);
			_decompressedStreams[i].Open(_compressedStreams[i]);
			_streamBuffers[i].Initialize(&_decompressedStreams[i]);
		}

		_videoFile = std::move(s);

		LOGI("Playing cinematic \"{}.j2v\" ({}x{}, {} frames, original format)", path, _width, _height, _framesLeft);

		LoadSfxList(path);

		return true;
	}

	bool Cinematics::LoadNativeVideo(std::unique_ptr<Stream>&& s, StringView path)
	{
		_nativeFormat = true;

		std::uint16_t version = s->ReadValueAsLE<std::uint16_t>();
		if (version > VideoFormat::CurrentVersion) {
			LOGE("Cannot load \"{}.j2v\" - Version {} is newer than supported", path, version);
			return false;
		}

		_width = s->ReadValueAsLE<std::uint16_t>();
		_height = s->ReadValueAsLE<std::uint16_t>();
		_frameDelay = s->ReadValueAsLE<std::uint16_t>() / (FrameTimer::SecondsPerFrame * 1000);
		_framesLeft = std::int32_t(s->ReadValueAsLE<std::uint32_t>());
		std::uint8_t pixelFormat = s->ReadValue<std::uint8_t>();
		std::uint8_t codec = s->ReadValue<std::uint8_t>();
		// Anything the writer added beyond what this build knows about is skipped
		std::uint16_t extensionSize = s->ReadValueAsLE<std::uint16_t>();
		if (extensionSize > 0) {
			s->Seek(extensionSize, SeekOrigin::Current);
		}

		if (pixelFormat != VideoFormat::PixelFormatIndexed8 || codec != VideoFormat::CodecDeltaRle) {
			LOGE("Cannot load \"{}.j2v\" - Unsupported pixel format {} or codec {}", path, pixelFormat, codec);
			return false;
		}
		if (_width == 0 || _height == 0 || _framesLeft <= 0) {
			LOGE("Cannot load \"{}.j2v\" - Unexpected dimensions", path);
			return false;
		}

		// Frames are already stored at the size they are shown at, so nothing is downscaled here beyond
		// what the hardware forces
		SetupFrameTextureSize(1);

		// Frames are palette indices. Where the hardware can sample those directly they are uploaded as they
		// are; where a paletted texture would have to be twiddled every frame they are converted instead (see
		// _convertTo565).
#if defined(DEATH_TARGET_DREAMCAST)
		_convertTo565 = true;
#else
		_convertTo565 = false;
#endif
		const Texture::Format textureFormat = (_convertTo565 ? Texture::Format::RGB565 : Texture::Format::R8);
		_textures[0] = std::make_unique<Texture>("Cinematics", textureFormat, _textureWidth, _textureHeight);
		_textures[1] = std::make_unique<Texture>("Cinematics", textureFormat, _textureWidth, _textureHeight);
		_textures[0]->SetMinFiltering(SamplerFilter::Nearest);
		_textures[0]->SetMagFiltering(SamplerFilter::Nearest);
		_textures[1]->SetMinFiltering(SamplerFilter::Nearest);
		_textures[1]->SetMagFiltering(SamplerFilter::Nearest);
		_paletteTexture = std::make_unique<Texture>("CinematicsPalette", Texture::Format::RGBA8, 256, 1);
		_paletteTexture->SetMinFiltering(SamplerFilter::Nearest);
		_paletteTexture->SetMagFiltering(SamplerFilter::Nearest);
		_paletteTexture->SetWrap(SamplerWrapping::ClampToEdge);
		// The entries reach the texture as raw uint32 values (see NormalizePaletteByteOrder above), so the
		// texel channel order follows the host's endianness exactly as it does for the shared palette
		ContentResolver::ConfigurePaletteTextureChannels(*_paletteTexture);
		_paletteDirty = true;
		_textureIndex = 0;
		// No _lastBuffer here: the native codec deltas in place inside _buffer (skipped spans just leave
		// the previous frame's pixels), so the legacy decoder's separate previous-frame copy is not needed
		_buffer = std::make_unique<std::uint8_t[]>(_width * _height);
		if (_videoDownscale > 1) {
			_indexedFrame = std::make_unique<std::uint8_t[]>(_textureWidth * _textureHeight);
		}
		if (_convertTo565) {
			_frameRow = std::make_unique<std::uint16_t[]>(_textureWidth);
		}
		std::memset(_buffer.get(), 0, _width * _height);

		_framePayloadCapacity = 0;
		// Over-allocated so the buffer itself can start on an aligned address
		_blockAllocation = std::make_unique<std::uint8_t[]>(VideoBlockCapacity + VideoBlockAlignment);
		_blockBuffer = reinterpret_cast<std::uint8_t*>(
			(reinterpret_cast<std::uintptr_t>(_blockAllocation.get()) + VideoBlockAlignment - 1)
				& ~std::uintptr_t(VideoBlockAlignment - 1));
		_blockSize = 0;
		_blockOffset = 0;
		_blockFilePosition = s->GetPosition();
		_videoFile = std::move(s);

		LOGI("Playing cinematic \"{}.j2v\" ({}x{}, {} frames)", path, _width, _height, _framesLeft);
		return true;
	}

	bool Cinematics::EnsureBuffered(std::uint32_t bytes)
	{
		if (_blockSize - _blockOffset >= bytes) {
			return true;
		}
		if (bytes > VideoBlockCapacity - VideoBlockAlignment) {
			return false;
		}

		// The Dreamcast's disc driver only transfers by DMA when both the destination and the position within
		// the file are 32 byte aligned (it checks `ptr & 31`), and falls back to PIO otherwise - which is
		// slower by orders of magnitude and turns each refill into a multi-second stall. So the block always
		// starts at an aligned offset in the file and is read into an aligned buffer, with whatever part of
		// the current frame precedes that offset simply re-read. Nothing is carried over, which also keeps
		// the destination aligned - a memmove'd remainder would push it off again.
		std::int64_t wanted = _blockFilePosition + _blockOffset;
		std::int64_t aligned = wanted & ~std::int64_t(VideoBlockAlignment - 1);

		_videoFile->Seek(aligned, SeekOrigin::Begin);
		std::int32_t bytesRead = _videoFile->Read(_blockBuffer, VideoBlockCapacity);
		_blockFilePosition = aligned;
		_blockSize = (bytesRead > 0 ? std::uint32_t(bytesRead) : 0);
		_blockOffset = std::uint32_t(wanted - aligned);
		return (_blockSize > _blockOffset && _blockSize - _blockOffset >= bytes);
	}

	void Cinematics::DecodeFrameNative()
	{
		// One length-prefixed payload per frame, served out of the read-ahead block
		if (!EnsureBuffered(4)) {
			_decodingFailed = true;
			return;
		}

		const std::uint8_t* sizeBytes = _blockBuffer + _blockOffset;
		std::uint32_t payloadSize = std::uint32_t(sizeBytes[0]) | (std::uint32_t(sizeBytes[1]) << 8)
			| (std::uint32_t(sizeBytes[2]) << 16) | (std::uint32_t(sizeBytes[3]) << 24);
		_blockOffset += 4;
		if (payloadSize == 0 || payloadSize > 8 * 1024 * 1024) {
			_decodingFailed = true;
			return;
		}

		const std::uint8_t* payload;
		// The same limit EnsureBuffered enforces - a refill can start up to an alignment's worth of
		// bytes before the payload, so a payload within that of the capacity still cannot fit
		if (payloadSize <= VideoBlockCapacity - VideoBlockAlignment) {
			if (!EnsureBuffered(payloadSize)) {
				_decodingFailed = true;
				return;
			}
			// Decoded straight out of the block, so there is no per-frame copy at all
			payload = _blockBuffer + _blockOffset;
			_blockOffset += payloadSize;
		} else {
			// A frame larger than the read-ahead block (e.g., the keyframe of a full-resolution video)
			// is read from the file directly. The file's physical position is wherever the last refill
			// ended, not the payload, so it has to be sought explicitly - and the block is dropped so
			// the next frame starts with a refill right past this payload
			if (payloadSize > _framePayloadCapacity) {
				_framePayload = std::make_unique<std::uint8_t[]>(payloadSize);
				_framePayloadCapacity = payloadSize;
			}
			_videoFile->Seek(_blockFilePosition + _blockOffset, SeekOrigin::Begin);
			if (_videoFile->Read(_framePayload.get(), payloadSize) != std::int32_t(payloadSize)) {
				_decodingFailed = true;
				return;
			}
			_blockFilePosition += std::int64_t(_blockOffset) + payloadSize;
			_blockOffset = 0;
			_blockSize = 0;
			payload = _framePayload.get();
		}

		const std::uint8_t* DEATH_RESTRICT src = payload;
		const std::uint8_t* const srcEnd = src + payloadSize;

		if ((*src++ & VideoFormat::FrameFlagPalette) != 0) {
			if (src + sizeof(_palette) > srcEnd) {
				_decodingFailed = true;
				return;
			}
			std::memcpy(_palette, src, sizeof(_palette));
			// The file's R,G,B,X byte order only matches the uint32 convention on little-endian hosts
			NormalizePaletteByteOrder(_palette, arraySize(_palette));
			src += sizeof(_palette);
			_paletteDirty = true;
		}

		// Skipped spans need no work at all: the buffer still holds the previous frame
		std::uint8_t* DEATH_RESTRICT dst = _buffer.get();
		std::uint8_t* const dstEnd = dst + std::size_t(_width) * _height;

		while (src < srcEnd) {
			const std::uint8_t command = *src++;
			if (command == VideoFormat::CommandEndOfFrame) {
				break;
			}

			std::int32_t length;
			if (command <= VideoFormat::CommandLiteralMax) {
				length = command + 1;
				if (src + length > srcEnd || dst + length > dstEnd) {
					break;
				}
				std::memcpy(dst, src, std::size_t(length));
				src += length;
				dst += length;
			} else if (command <= VideoFormat::CommandRunMax) {
				length = command - VideoFormat::CommandRunBase + VideoFormat::CommandRunMinLength;
				if (src >= srcEnd || dst + length > dstEnd) {
					break;
				}
				std::memset(dst, *src++, std::size_t(length));
				dst += length;
			} else if (command <= VideoFormat::CommandSkipMax) {
				dst += command - VideoFormat::CommandSkipBase + 1;
			} else if (command == VideoFormat::CommandSkipLong) {
				if (src + 2 > srcEnd) {
					break;
				}
				length = src[0] | (src[1] << 8);
				src += 2;
				dst += length;
			} else if (command == VideoFormat::CommandLiteralLong) {
				if (src + 2 > srcEnd) {
					break;
				}
				length = src[0] | (src[1] << 8);
				src += 2;
				if (src + length > srcEnd || dst + length > dstEnd) {
					break;
				}
				std::memcpy(dst, src, std::size_t(length));
				src += length;
				dst += length;
			} else if (command == VideoFormat::CommandRunLong) {
				if (src + 3 > srcEnd) {
					break;
				}
				length = src[0] | (src[1] << 8);
				src += 2;
				if (dst + length > dstEnd) {
					break;
				}
				std::memset(dst, *src++, std::size_t(length));
				dst += length;
			} else {
				break;
			}

			if (dst > dstEnd) {
				break;
			}
		}
	}

	bool Cinematics::LoadSfxList(StringView path)
	{
#if defined(WITH_AUDIO)
		auto& resolver = ContentResolver::Get();
		auto s = resolver.OpenContentFile(fs::CombinePath("Cinematics"_s, String(path + ".j2sfx"_s)));
		if (!s->IsValid()) {
			return false;
		}

		if (s->GetSize() <= 16 || s->GetSize() >= 64 * 1024 * 1024) {
			LOGE("Cannot load SFX playlist for \"{}.j2v\" - Unexpected file size", path);
			return false;
		}

		std::uint64_t signature = s->ReadValueAsLE<std::uint64_t>();
		std::uint8_t fileType = s->ReadValue<std::uint8_t>();
		std::uint16_t version = s->ReadValueAsLE<std::uint16_t>();
		if (signature != 0x2095A59FF0BFBBEF || fileType != ContentFileType::SfxList || version > SfxListVersion) {
			LOGE("Cannot load SFX playlist for \"{}.j2v\" - Invalid signature", path);
			return false;
		}

		std::uint32_t sampleCount = s->ReadValueAsLE<std::uint16_t>();
		for (std::uint32_t i = 0; i < sampleCount; i++) {
			std::uint8_t stringSize = s->ReadValue<std::uint8_t>();
			String samplePath = String(NoInit, stringSize);
			s->Read(samplePath.data(), stringSize);

			String samplePathNormalized = fs::ToNativeSeparators(samplePath);
			String fullPath = fs::CombinePath("Animations"_s, samplePathNormalized);
			auto sample = resolver.OpenContentFile(fullPath);
			if (sample->IsValid()) {
				_sfxSamples.emplace_back(std::move(sample), fullPath);
			} else {
				_sfxSamples.emplace_back(); // Sample not found
			}
		}

		std::uint32_t itemCount = s->ReadValueAsLE<std::uint16_t>();
		for (std::uint32_t i = 0; i < itemCount; i++) {
			auto& item = _sfxPlaylist.emplace_back();
			item.Frame = s->ReadVariableUint32();
			item.Sample = s->ReadValueAsLE<std::uint16_t>();
			item.Gain = s->ReadValue<std::uint8_t>() / 255.0f;
			item.Panning = s->ReadValue<std::int8_t>() / 127.0f;
		}

		return true;
#else
		return false;
#endif
	}

	void Cinematics::PrepareNextFrame()
	{
		if (_nativeFormat) {
			DecodeFrameNative();
		} else {
			DecodeFrameLegacy();
		}

		if (_paletteDirty) {
			_paletteDirty = false;
			std::memcpy(_uploadPalette, _palette, sizeof(_uploadPalette));
			_paletteTextureDirty = true;
		}

		ApplyPaletteAndUpload(_buffer.get());

		PlayFrameSounds();
	}

	void Cinematics::DecodeFrameLegacy()
	{
		// Check if palette was changed
		if (ReadByte(0) == 0x01) {
			Read(3, _palette, sizeof(_palette));
			// The file's R,G,B,X byte order only matches the uint32 convention on little-endian hosts
			NormalizePaletteByteOrder(_palette, arraySize(_palette));
			_paletteDirty = true;
		}

		// Read pixels into the buffer. Both kinds of run are copied in one go rather than a pixel at a
		// time - a frame is a few hundred thousand pixels, and per-pixel calls into the stream dominated
		// the decoding cost on the slower platforms.
		const std::int32_t rowWidth = std::int32_t(_width);
		const std::int32_t totalPixels = rowWidth * std::int32_t(_height);
		for (std::int32_t y = 0; y < std::int32_t(_height) && !_decodingFailed; y++) {
			std::uint8_t* DEATH_RESTRICT row = &_buffer[std::size_t(y) * rowWidth];
			std::uint8_t c;
			std::int32_t x = 0;
			while ((c = ReadByte(0)) != 0x80) {
				// A dead stream keeps returning zeros (c = 0 with a zero run length), which would spin
				// here forever - bail out as soon as the short read is detected
				if DEATH_UNLIKELY(_decodingFailed) {
					return;
				}
				if (c < 0x80) {
					// Run of new pixels
					std::int32_t u = (c == 0x00 ? AsLE(ReadValue<std::uint16_t>(0)) : c);
					const std::int32_t fits = std::min(u, rowWidth - x);
					if (fits > 0) {
						Read(3, &row[x], std::uint32_t(fits));
					}
					// A run overhanging the row would be a corrupted frame; its bytes are still consumed
					// so the stream stays aligned with the following runs
					if (u > fits) {
						Skip(3, std::uint32_t(u - fits));
					}
					x += u;
				} else {
					// Run copied from the previous frame
					std::int32_t u = (c == 0x81 ? AsLE(ReadValue<std::uint16_t>(0)) : c - 0x6A);
					const std::int32_t n = AsLE(ReadValue<std::uint16_t>(1)) + (ReadByte(2) + y - 127) * rowWidth;
					const std::int32_t fits = std::min(u, rowWidth - x);
					if (fits > 0 && n >= 0 && n + fits <= totalPixels) {
						std::memcpy(&row[x], &_lastBuffer[n], std::size_t(fits));
					}
					x += u;
				}
			}
		}

		// Keep a copy of this frame; the next one is encoded as changes against it
		std::memcpy(_lastBuffer.get(), _buffer.get(), _width * _height);
	}

	void Cinematics::ApplyPaletteAndUpload(const std::uint8_t* indices)
	{
		// The palette only changes on a few frames, and it is a single 256 entry row
		if (_paletteTextureDirty) {
			_paletteTextureDirty = false;
			if (_convertTo565) {
				// Packed once here, so converting a frame is a lookup and a 16-bit store per pixel
				for (std::int32_t i = 0; i < 256; i++) {
					const std::uint32_t color = _uploadPalette[i];
					_palette565[i] = std::uint16_t((((color >> 3) & 0x1F) << 11)
						| ((((color >> 8) >> 2) & 0x3F) << 5) | (((color >> 16) >> 3) & 0x1F));
				}
			} else {
				std::uint32_t opaquePalette[256];
				for (std::int32_t i = 0; i < 256; i++) {
					// Videos are opaque; entry 0 of a sprite palette is transparent, which must not apply here
					opaquePalette[i] = _uploadPalette[i] | 0xFF000000u;
				}
				_paletteTexture->LoadFromTexels((std::uint8_t*)opaquePalette, 0, 0, 256, 1);
			}
		}

		// The indices go to the GPU as they are - no palette is applied on the CPU at all. Only a video that
		// is still larger than its texture needs a pass here, and even that one just picks every n-th index.
		const std::uint8_t* texels = indices;
		if (_videoDownscale > 1) {
			const std::uint32_t step = _videoDownscale;
			std::uint8_t* dst = _indexedFrame.get();
			for (std::uint32_t y = 0; y < _textureHeight; y++) {
				const std::uint8_t* src = &indices[y * step * _width];
				for (std::uint32_t x = 0; x < _textureWidth; x++) {
					*dst++ = src[x * step];
				}
			}
			texels = _indexedFrame.get();
		}

		// Upload new texture to GPU (into the buffer the GPU is not currently sampling)
		_textureIndex ^= 1;

		if (_convertTo565) {
#if defined(RHI_CAP_STREAMING_TEXTURES)
			// Where the texture's storage can be written directly, the frame is converted into it a row at a
			// time. The obvious way - convert the whole frame into a buffer and hand that to LoadFromTexels -
			// walks the frame three times over: once to convert, once to copy it into the texture's host copy,
			// once to copy that where the hardware reads it. A row stays in the cache between being converted
			// and being copied out, so only the last of those three passes is left.
			std::int32_t strideBytes = 0;
			if (std::uint8_t* mapped = static_cast<std::uint8_t*>(_textures[_textureIndex]->MapStreamingTexels(strideBytes))) {
				const std::size_t rowBytes = std::size_t(_textureWidth) * 2;
				std::uint16_t* DEATH_RESTRICT row = _frameRow.get();
#if defined(DEATH_TARGET_DREAMCAST)
				// The rows go out through the store queues, which burst 32 bytes at a time instead of pushing
				// each write to video memory on its own. Their requirements - a 32-byte aligned destination, a
				// 32-bit aligned source and a whole number of blocks - are all satisfied by the usual sizes,
				// but not by every possible one, so the plain copy stays as the alternative.
				// (sq_fast_cpy wants the source 8-byte aligned, which is stricter than the 4 bytes the wide
				// conversion above needs.)
				const bool useStoreQueues = ((rowBytes & 31) == 0 && (strideBytes & 31) == 0
					&& (reinterpret_cast<std::uintptr_t>(mapped) & 31) == 0
					&& (reinterpret_cast<std::uintptr_t>(row) & 7) == 0);
				if (useStoreQueues) {
					sq_lock(mapped);
					for (std::uint32_t y = 0; y < _textureHeight; y++) {
						ConvertIndicesTo565(&texels[std::size_t(y) * _textureWidth], row, _palette565, _textureWidth);
						sq_fast_cpy(SQ_MASK_DEST(mapped + std::size_t(y) * strideBytes), row, rowBytes / 32);
					}
					sq_unlock();
					return;
				}
#endif
				for (std::uint32_t y = 0; y < _textureHeight; y++) {
					ConvertIndicesTo565(&texels[std::size_t(y) * _textureWidth], row, _palette565, _textureWidth);
#if defined(DEATH_TARGET_DREAMCAST)
					// Whatever ruled the store queues out, accesses to video memory still have to stay
					// 16/32-bit wide, which libc memcpy does not guarantee for every size and alignment
					std::uint16_t* DEATH_RESTRICT dst = reinterpret_cast<std::uint16_t*>(mapped + std::size_t(y) * strideBytes);
					for (std::uint32_t x = 0; x < _textureWidth; x++) {
						dst[x] = row[x];
					}
#else
					std::memcpy(mapped + std::size_t(y) * strideBytes, row, rowBytes);
#endif
				}
				return;
			}
#endif
			// The whole-frame buffer is only needed by this fallback, so it is allocated if it is ever reached
			if (_frame565 == nullptr) {
				_frame565 = std::make_unique<std::uint16_t[]>(std::size_t(_textureWidth) * _textureHeight);
			}
			ConvertIndicesTo565(texels, _frame565.get(), _palette565, _textureWidth * _textureHeight);
			texels = reinterpret_cast<const std::uint8_t*>(_frame565.get());
		}

		_textures[_textureIndex]->LoadFromTexels(texels, 0, 0, _textureWidth, _textureHeight);
	}

	void Cinematics::PlayFrameSounds()
	{
#if defined(WITH_AUDIO)
		for (std::size_t i = 0; i < _sfxPlaylist.size(); i++) {
			if (_sfxPlaylist[i].Frame == _frameIndex) {
				auto& item = _sfxPlaylist[i];
				auto& sample = _sfxSamples[item.Sample];
				if (sample.Buffer == nullptr) {
					continue;
				}

				item.CurrentPlayer = std::make_unique<nCine::AudioBufferPlayer>(sample.Buffer.get());
				item.CurrentPlayer->setPosition(Vector3f(item.Panning, 0.0f, 0.0f));
				item.CurrentPlayer->setAs2D(true);
				item.CurrentPlayer->setGain(_sfxPlaylist[i].Gain * PreferencesCache::MasterVolume * PreferencesCache::SfxVolume);
				item.CurrentPlayer->play();
			}
		}
#endif

		_frameIndex++;
	}

	void Cinematics::StreamBuffer::Initialize(Stream* source)
	{
		Source = source;
		if (Data == nullptr) {
			Data = std::make_unique<std::uint8_t[]>(Capacity);
		}
		Position = 0;
		Length = 0;
	}

	std::int32_t Cinematics::StreamBuffer::Refill()
	{
		Position = 0;
		Length = 0;
		if (Source == nullptr) {
			return 0;
		}
		const std::int64_t bytesRead = Source->Read(Data.get(), Capacity);
		if (bytesRead > 0) {
			Length = std::int32_t(bytesRead);
		}
		return Length;
	}

	void Cinematics::Skip(std::int32_t streamIndex, std::uint32_t bytes)
	{
		StreamBuffer& src = _streamBuffers[streamIndex];
		while (bytes > 0) {
			const std::int32_t available = src.Length - src.Position;
			if (available <= 0) {
				if (src.Refill() <= 0) {
					return;
				}
				continue;
			}
			const std::uint32_t n = (bytes < std::uint32_t(available) ? bytes : std::uint32_t(available));
			src.Position += std::int32_t(n);
			bytes -= n;
		}
	}

	void Cinematics::Read(std::int32_t streamIndex, void* buffer, std::uint32_t bytes)
	{
		StreamBuffer& src = _streamBuffers[streamIndex];
		std::uint8_t* dst = static_cast<std::uint8_t*>(buffer);
		std::uint32_t remaining = bytes;

		while (remaining > 0) {
			const std::int32_t available = src.Length - src.Position;
			if (available <= 0) {
				if (src.Refill() <= 0) {
					break;
				}
				continue;
			}
			const std::uint32_t n = (remaining < std::uint32_t(available) ? remaining : std::uint32_t(available));
			std::memcpy(dst, &src.Data[src.Position], n);
			src.Position += std::int32_t(n);
			dst += n;
			remaining -= n;
		}

		std::int64_t bytesRead = std::int64_t(bytes - remaining);
		if DEATH_UNLIKELY(bytesRead < std::int64_t(bytes)) {
			if (!_decodingFailed) {
				_decodingFailed = true;
				LOGE("Failed to read {} bytes from stream {} at frame {}", bytes, streamIndex, _frameIndex);
			}
			if (bytesRead < 0) {
				bytesRead = 0;
			}
			std::memset(static_cast<std::uint8_t*>(buffer) + bytesRead, 0, bytes - std::uint32_t(bytesRead));
		}
	}

	void Cinematics::UpdatePressedActions()
	{
		auto& input = theApplication().GetInputManager();
		_pressedActions = ((_pressedActions & 0xFFFF) << 16);

		const JoyMappedState* joyStates[ControlScheme::MaxConnectedGamepads];
		std::int32_t joyStatesCount = 0;
		for (std::int32_t i = 0; i < JoyMapping::MaxNumJoysticks && joyStatesCount < std::int32_t(arraySize(joyStates)); i++) {
			if (input.isJoyMapped(i)) {
				joyStates[joyStatesCount++] = &input.joyMappedState(i);
			}
		}

		_pressedActions |= ControlScheme::FetchNavigation(_pressedKeys, ArrayView(joyStates, joyStatesCount));

		// Also allow Menu action as skip key
		if (_pressedActions & (1 << (std::uint32_t)PlayerAction::Menu)) {
			_pressedActions |= (1 << (std::uint32_t)PlayerAction::Fire);
		}
	}

	void Cinematics::CinematicsCanvas::Initialize()
	{
		// Prepare output render command
		_renderCommand.SetType(RenderCommand::Type::Sprite);
		// Indexed frames are sampled through the palette; converted ones are plain textures
		ContentResolver::Get().ConfigureSpriteShader(_renderCommand, !_owner->_convertTo565);
		_renderCommand.GetMaterial().ReserveUniformsDataMemory();
		_renderCommand.GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);

		auto* textureUniform = _renderCommand.GetMaterial().Uniform(Material::TextureUniformName);
		if (textureUniform && textureUniform->GetIntValue(0) != 0) {
			textureUniform->SetIntValue(0); // GL_TEXTURE0
		}
	}

	bool Cinematics::CinematicsCanvas::OnDraw(RenderQueue& renderQueue)
	{
		if (_owner->_frameDelay == 0.0f) {
			return false;
		}

		Vector2i viewSize = _owner->_upscalePass.GetViewSize();
		float ratioTarget = (float)viewSize.Y / viewSize.X;
		float ratioSource = (float)_owner->_height / _owner->_width;

		Vector2f frameSize;
		if (PreferencesCache::KeepAspectRatioInCinematics) {
			if (ratioTarget < ratioSource) {
				frameSize = Vector2f(viewSize.Y / ratioSource, viewSize.Y);
			} else {
				frameSize = Vector2f(viewSize.X, viewSize.X * ratioSource);
			}
		} else {
			// Try to adjust ratio a bit, otherwise show black bars or zoom it in
			float ratio = std::clamp(ratioTarget, ratioSource - 0.16f, ratioSource);
			frameSize = Vector2f(viewSize.X, viewSize.X * ratio);
		}

		Vector2f frameOffset = (viewSize.As<float>() - frameSize) * 0.5f;
		frameOffset.X = std::round(frameOffset.X);
		frameOffset.Y = std::round(frameOffset.Y);

		auto* instanceBlock = _renderCommand.GetMaterial().UniformBlock(Material::InstanceBlockName);
		instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
		instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatVector(frameSize.Data());
		instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf::White.Data());

		_renderCommand.SetTransformation(Matrix4x4f::Translation(frameOffset.X, frameOffset.Y, 0.0f));
		_renderCommand.GetMaterial().SetTexture(0, *_owner->_textures[_owner->_textureIndex]);
		if (!_owner->_convertTo565) {
			_renderCommand.GetMaterial().SetTexture(1, *_owner->_paletteTexture);
			if (auto* palOffset = instanceBlock->GetUniform(Material::PaletteOffsetUniformName)) {
				palOffset->SetFloatValue(0.0f);
			}
		}

		renderQueue.AddCommand(&_renderCommand);

		return true;
	}

	void Cinematics::FileWindow::Initialize(Stream* file, std::int64_t startOffset)
	{
		_file = file;
		if (_data == nullptr) {
			_data = std::make_unique<std::uint8_t[]>(WindowSize);
		}
		_start = startOffset;
		_length = 0;
	}

	std::int32_t Cinematics::FileWindow::Read(std::int64_t offset, void* destination, std::int32_t bytes)
	{
		if (_file == nullptr || bytes <= 0) {
			return 0;
		}

		// Anything larger than the window goes straight to the file; the streams never ask for that much
		if (bytes > WindowSize) {
			if (_file->Seek(offset, SeekOrigin::Begin) < 0) {
				return 0;
			}
			_length = 0;
			return std::int32_t(_file->Read(destination, bytes));
		}

		if (offset < _start || offset + bytes > _start + _length) {
			// Refill, starting a little before the request so the streams trailing this one stay covered
			const std::int64_t newStart = (offset > WindowMargin ? offset - WindowMargin : 0);
			if (_file->Seek(newStart, SeekOrigin::Begin) < 0) {
				return 0;
			}
			const std::int64_t bytesRead = _file->Read(_data.get(), WindowSize);
			_start = newStart;
			_length = (bytesRead > 0 ? std::int32_t(bytesRead) : 0);
			const std::int64_t availableAt = _start + _length - offset;
			if (availableAt <= 0) {
				return 0;
			}
			if (bytes > availableAt) {
				bytes = std::int32_t(availableAt);
			}
		}

		std::memcpy(destination, &_data[offset - _start], std::size_t(bytes));
		return bytes;
	}

	Cinematics::ChunkedStream::ChunkedStream()
		: _window(nullptr), _size(0), _position(0), _chunkIndex(0), _chunkStart(0)
	{
	}

	void Cinematics::ChunkedStream::Initialize(FileWindow* window, SmallVector<Pair<std::int64_t, std::int32_t>, 0>&& chunks, std::int64_t initialOffset)
	{
		_window = window;
		_chunks = std::move(chunks);
		_size = 0;
		for (auto& chunk : _chunks) {
			_size += chunk.second();
		}
		_position = initialOffset;
		_chunkIndex = 0;
		_chunkStart = 0;
	}

	void Cinematics::ChunkedStream::Dispose()
	{
		_window = nullptr;
		_chunks.clear();
	}

	std::int64_t Cinematics::ChunkedStream::Seek(std::int64_t offset, SeekOrigin origin)
	{
		switch (origin) {
			case SeekOrigin::Begin: _position = offset; break;
			case SeekOrigin::Current: _position += offset; break;
			case SeekOrigin::End: _position = _size + offset; break;
		}
		return _position;
	}

	std::int64_t Cinematics::ChunkedStream::GetPosition() const
	{
		return _position;
	}

	std::int64_t Cinematics::ChunkedStream::Read(void* destination, std::int64_t bytesToRead)
	{
		if (_window == nullptr || bytesToRead <= 0) {
			return 0;
		}

		std::uint8_t* typedBuffer = static_cast<std::uint8_t*>(destination);
		std::int64_t bytesReadTotal = 0;

		// Find the chunk containing the current position, starting from the cached cursor (reads are
		// mostly sequential), and read across chunk boundaries
		if (_position < _chunkStart) {
			_chunkIndex = 0;
			_chunkStart = 0;
		}
		while (_chunkIndex < _chunks.size() && bytesToRead > 0) {
			const auto& chunk = _chunks[_chunkIndex];
			const std::int64_t chunkSize = chunk.second();
			if (_position < _chunkStart + chunkSize) {
				const std::int64_t within = _position - _chunkStart;
				const std::int64_t n = std::min(bytesToRead, chunkSize - within);
				// The source is shared by the four interleaved streams; skip the seek when the previous
				// read already left it at the right offset (a plain seek still costs syscalls)
				const std::int64_t sourceOffset = chunk.first() + within;
				const std::int64_t bytesRead = _window->Read(sourceOffset, &typedBuffer[bytesReadTotal], std::int32_t(n));
				if (bytesRead <= 0) {
					break;
				}
				bytesReadTotal += bytesRead;
				_position += bytesRead;
				bytesToRead -= bytesRead;
				if (bytesToRead <= 0) {
					break;
				}
			}
			_chunkStart += chunkSize;
			_chunkIndex++;
		}

		return bytesReadTotal;
	}

	std::int64_t Cinematics::ChunkedStream::Write(const void* source, std::int64_t bytesToWrite)
	{
		// Not supported
		return Stream::Invalid;
	}

	bool Cinematics::ChunkedStream::Flush()
	{
		return true;
	}

	bool Cinematics::ChunkedStream::IsValid()
	{
		return (_window != nullptr && !_chunks.empty());
	}

	std::int64_t Cinematics::ChunkedStream::GetSize() const
	{
		return _size;
	}

	std::int64_t Cinematics::ChunkedStream::SetSize(std::int64_t size)
	{
		// Not supported
		return Stream::Invalid;
	}

#if defined(WITH_AUDIO)
	Cinematics::SfxItem::SfxItem()
	{
	}

	Cinematics::SfxItem::SfxItem(std::unique_ptr<Stream> stream, StringView path)
		: Buffer(std::make_unique<AudioBuffer>(std::move(stream), path))
	{
	}
#endif
}