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
#	include <kos/thread.h>
#elif defined(DEATH_TARGET_N64)
#	include "../../nCine/Backends/N64/N64FullMotionVideo.h"
#endif
#if defined(WITH_LZ4)
#	include <lz4.h>
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
			frame conversion costs most of its time in. Spans the decoder converts start anywhere in a frame, so
			up to three leading pixels are converted one at a time to align the source, which aligns a
			destination at the same offset into its frame as well. A source that still cannot be aligned with
			its destination leaves two colors per store at least.
		*/
		DEATH_ALWAYS_INLINE void ConvertIndicesTo565(const std::uint8_t* DEATH_RESTRICT src,
			std::uint16_t* DEATH_RESTRICT dst, const std::uint16_t* DEATH_RESTRICT palette, std::uint32_t count)
		{
			std::uint32_t i = 0;
#if !defined(DEATH_TARGET_BIG_ENDIAN)
			while (i < count && i < 3 && ((reinterpret_cast<std::uintptr_t>(src + i) | reinterpret_cast<std::uintptr_t>(dst + i)) & 3) != 0) {
				dst[i] = palette[src[i]];
				i++;
			}
			if (((reinterpret_cast<std::uintptr_t>(src + i) | reinterpret_cast<std::uintptr_t>(dst + i)) & 3) == 0) {
				const std::uint32_t* DEATH_RESTRICT src32 = reinterpret_cast<const std::uint32_t*>(src + i);
				std::uint32_t* DEATH_RESTRICT dst32 = reinterpret_cast<std::uint32_t*>(dst + i);
				const std::uint32_t quads = (count - i) / 4;
				for (std::uint32_t q = 0; q < quads; q++) {
					const std::uint32_t four = src32[q];
					const std::uint32_t c0 = palette[four & 0xFF];
					const std::uint32_t c1 = palette[(four >> 8) & 0xFF];
					const std::uint32_t c2 = palette[(four >> 16) & 0xFF];
					const std::uint32_t c3 = palette[four >> 24];
					dst32[(q * 2) + 0] = c0 | (c1 << 16);
					dst32[(q * 2) + 1] = c2 | (c3 << 16);
				}
				i += quads * 4;
			} else {
				if ((reinterpret_cast<std::uintptr_t>(dst + i) & 3) != 0 && i < count) {
					dst[i] = palette[src[i]];
					i++;
				}
				std::uint32_t* DEATH_RESTRICT dst32 = reinterpret_cast<std::uint32_t*>(dst + i);
				const std::uint32_t pairs = (count - i) / 2;
				for (std::uint32_t p = 0; p < pairs; p++) {
					const std::uint32_t c0 = palette[src[i + p * 2]];
					const std::uint32_t c1 = palette[src[i + p * 2 + 1]];
					dst32[p] = c0 | (c1 << 16);
				}
				i += pairs * 2;
			}
#endif
			for (; i < count; i++) {
				dst[i] = palette[src[i]];
			}
		}

		/** @brief Fills @p count pixels with one RGB565 color, two of them per store where the destination allows */
		DEATH_ALWAYS_INLINE void Fill565(std::uint16_t* DEATH_RESTRICT dst, std::uint16_t color, std::uint32_t count)
		{
			std::uint32_t i = 0;
			if ((reinterpret_cast<std::uintptr_t>(dst) & 3) != 0 && count > 0) {
				dst[0] = color;
				i = 1;
			}
			std::uint32_t* DEATH_RESTRICT dst32 = reinterpret_cast<std::uint32_t*>(dst + i);
			const std::uint32_t pair = std::uint32_t(color) | (std::uint32_t(color) << 16);
			const std::uint32_t pairs = (count - i) / 2;
			for (std::uint32_t p = 0; p < pairs; p++) {
				dst32[p] = pair;
			}
			i += pairs * 2;
			if (i < count) {
				dst[i] = color;
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

#if defined(WITH_LZ4) && defined(DEATH_TARGET_DREAMCAST)
		/** @brief Copies a short span four bytes at a time, which halves the loop overhead of a byte-wise copy */
		DEATH_ALWAYS_INLINE void CopyBytes(std::uint8_t*& op, const std::uint8_t*& ip, std::size_t length)
		{
			for (; length >= 4; length -= 4) {
				op[0] = ip[0];
				op[1] = ip[1];
				op[2] = ip[2];
				op[3] = ip[3];
				op += 4;
				ip += 4;
			}
			for (; length > 0; length--) {
				*op++ = *ip++;
			}
		}

		/**
			@brief Decompresses one LZ4 block, returning its size or -1 if it is malformed or does not fit

			The library's decoder is written for processors that load and store at any alignment: it copies
			literals and matches in 8 and 16 byte strides through `memcpy()`, which compilers turn into single
			unaligned moves. The SH-4 has none, so each of those strides becomes a call into the C library: a
			640x480 frame took 3 ms to decompress on average and up to 45 ms (in Flycast), against 1.3 and 18 ms
			this way. Here a short copy is a plain byte loop and only a long one goes through `memcpy()` - and a
			match overlapping its own output by less than four bytes (a repeated pattern) is copied byte by byte.
		*/
		std::int32_t DecompressLz4Block(const std::uint8_t* DEATH_RESTRICT src, std::size_t srcSize,
			std::uint8_t* DEATH_RESTRICT dst, std::size_t dstCapacity)
		{
			constexpr std::size_t LongCopy = 32;

			const std::uint8_t* ip = src;
			const std::uint8_t* const ipEnd = src + srcSize;
			std::uint8_t* op = dst;
			std::uint8_t* const opEnd = dst + dstCapacity;

			while (ip < ipEnd) {
				const std::uint32_t token = *ip++;
				std::size_t length = token >> 4;
				if (length == 15) {
					std::uint32_t extra;
					do {
						if (ip >= ipEnd) {
							return -1;
						}
						extra = *ip++;
						length += extra;
					} while (extra == 255);
				}
				if (length > std::size_t(ipEnd - ip) || length > std::size_t(opEnd - op)) {
					return -1;
				}
				if (length >= LongCopy) {
					std::memcpy(op, ip, length);
					op += length;
					ip += length;
				} else {
					CopyBytes(op, ip, length);
				}
				if (ip >= ipEnd) {
					// The last sequence of a block has only literals
					break;
				}

				if (ipEnd - ip < 2) {
					return -1;
				}
				const std::size_t offset = std::size_t(ip[0]) | (std::size_t(ip[1]) << 8);
				ip += 2;
				if (offset == 0 || offset > std::size_t(op - dst)) {
					return -1;
				}
				length = (token & 15) + 4;
				if ((token & 15) == 15) {
					std::uint32_t extra;
					do {
						if (ip >= ipEnd) {
							return -1;
						}
						extra = *ip++;
						length += extra;
					} while (extra == 255);
				}
				if (length > std::size_t(opEnd - op)) {
					return -1;
				}
				const std::uint8_t* match = op - offset;
				if (offset >= length && length >= LongCopy) {
					std::memcpy(op, match, length);
					op += length;
				} else if (offset >= 4) {
					// Four bytes behind or more, a group of four never reads what the same group writes
					CopyBytes(op, match, length);
				} else {
					for (std::size_t i = 0; i < length; i++) {
						*op++ = *match++;
					}
				}
			}
			return std::int32_t(op - dst);
		}
#endif

		/**
			@brief Executes the commands of one frame of the native format (see @ref VideoFormat)

			With @p WithColors, every pixel written is also written to @p colors already converted through
			@p palette565, which keeps a converted copy of the frame current at the cost of the pixels that
			changed - instead of converting the whole frame again after it was decoded.
		*/
		template<bool WithColors>
		void DecodeCommands(const std::uint8_t* DEATH_RESTRICT src, const std::uint8_t* const srcEnd,
			std::uint8_t* DEATH_RESTRICT const frame, const std::size_t frameSize, std::uint16_t* DEATH_RESTRICT const colors,
			const std::uint16_t* DEATH_RESTRICT const palette565)
		{
			std::uint8_t* DEATH_RESTRICT dst = frame;
			std::uint8_t* const dstEnd = frame + frameSize;

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
					if constexpr (WithColors) {
						// From the frame rather than the payload: at the same offset as the colors, both align
						// together, which lets the conversion take four pixels per load
						ConvertIndicesTo565(dst, colors + (dst - frame), palette565, std::uint32_t(length));
					}
					src += length;
					dst += length;
				} else if (command <= VideoFormat::CommandRunMax) {
					length = command - VideoFormat::CommandRunBase + VideoFormat::CommandRunMinLength;
					if (src >= srcEnd || dst + length > dstEnd) {
						break;
					}
					if constexpr (WithColors) {
						Fill565(colors + (dst - frame), palette565[*src], std::uint32_t(length));
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
					if constexpr (WithColors) {
						// From the frame rather than the payload: at the same offset as the colors, both align
						// together, which lets the conversion take four pixels per load
						ConvertIndicesTo565(dst, colors + (dst - frame), palette565, std::uint32_t(length));
					}
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
					if constexpr (WithColors) {
						Fill565(colors + (dst - frame), palette565[*src], std::uint32_t(length));
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

		/**
			@brief Size of the read-ahead ring of the native format (see @ref Cinematics::VideoReader)

			The consoles that play their video at full resolution need about 20 KB a frame on average, and a
			few hundred kilobytes in a row at a scene cut, so they get a ring of about a second of video.
			Everything else plays a quarter of that.
		*/
		constexpr std::uint32_t VideoReadAheadCapacity =
#if defined(DEATH_TARGET_DREAMCAST) || defined(DEATH_TARGET_GAMECUBE) || defined(DEATH_TARGET_WII)
			1024 * 1024;
#else
			256 * 1024;
#endif

#if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)
		/** @brief Whether the read-ahead runs on a thread of its own */
		constexpr bool VideoReadInBackground = true;
		/** @brief Size of one read: a thread can afford large ones, which keep the drive's per-request overhead low */
		constexpr std::uint32_t VideoReadPieceSize = 64 * 1024;
#else
		constexpr bool VideoReadInBackground = false;
		/**
			@brief Size of one read: without a thread a read blocks the frame it happens in, so it has to fit into
			the time such a frame would otherwise spend waiting for the display
		*/
		constexpr std::uint32_t VideoReadPieceSize = 16 * 1024;
#endif
		/** @brief How much is read before playback starts, so the first frames never wait for the drive */
		constexpr std::uint32_t VideoPrefillBytes = 128 * 1024;
	}
	Cinematics::Cinematics(IRootController* root, StringView path, Function<bool(IRootController*, bool)>&& callback)
		: _root(root), _callback(Death::move(callback)), _frameDelay(0.0f), _frameProgress(0.0f), _framesLeft(0), _frameIndex(0),
			_videoDownscale(1), _textureWidth(0), _textureHeight(0), _textureIndex(0),
			_pressedKeys(ValueInit, (std::size_t)Keys::Count), _pressedActions(0), _decodingFailed(false),
			_nativeFormat(false), _lz4Payloads(false), _convertTo565(false), _incremental565(false), _frame565Stale(false), _paletteDirty(true),
			_paletteTextureDirty(false)
	{
		Initialize(path);
	}

	Cinematics::~Cinematics()
	{
		_canvas->setParent(nullptr);
		// The reader thread (if any) reads into memory and from a file that are released right after this
		_videoReader.Shutdown();
	}

	Vector2i Cinematics::GetViewSize() const
	{
		return _upscalePass.GetViewSize();
	}

	void Cinematics::OnBeginFrame()
	{
#if defined(DEATH_TARGET_N64)
		if (!_fullMotionVideoPath.empty()) {
			// Blocks for the length of the video. This is the one point of a frame where the renderer holds
			// neither the display nor any RDP state: the previous frame has been presented and this one has not
			// drawn anything yet.
			String videoPath = Death::move(_fullMotionVideoPath);
			const bool completed = Backends::N64FullMotionVideo::Play(videoPath, PreferencesCache::MasterVolume);
			_framesLeft = 0;
			if (_callback && _callback(_root, completed)) {
				_callback = nullptr;
			}
			return;
		}
#endif

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
#if defined(DEATH_TARGET_DREAMCAST) || defined(DEATH_TARGET_N64) || defined(DEATH_TARGET_WII) || \
		defined(DEATH_TARGET_GAMECUBE) || defined(DEATH_TARGET_3DS) || defined(DEATH_TARGET_PS2)
		// On a machine that decodes slower than the video's frame rate, catching up buys nothing: it hides
		// all but the last of the decoded frames while making the stall worse. At most one frame is decoded
		// per rendered frame, every frame is shown, and the picture may fall behind the music.
		//
		// The PlayStation 2 is on this list for the display rather than the decoder: it presents on vertical
		// sync at 60 Hz and the cinematics run at 24, so one decode per presented frame is already more than
		// twice what the video asks for. Letting it decode three in a row could therefore only happen once
		// it was already behind - and it turned a frame that was merely late into one three times as long,
		// which is what a burst of catch-up looks like on a television.
		constexpr std::int32_t MaxDecodesPerFrame = 1;
		// What is still owed, on the other hand, is kept, up to about half a second of video. The time the
		// display runs ahead of the video is not a backlog at all most of the time: at 60 Hz a 42 ms frame is
		// due after 2.52 presented frames, so it is decoded on the third, and the 0.48 frames of time left
		// over are what puts the one after it on the second. Capping the progress at a single frame threw
		// that remainder away every time - each frame then took three presented frames, 50 ms instead of 42,
		// and the video ran at 20 frames per second, 6.5 seconds behind the music by the end of the intro on
		// every console listed here, before a single frame was late for any other reason. A real stall (a
		// slow read, a frame that missed the blank) is then caught up as well, one frame per presented
		// frame, so the picture and the music come back together within a few frames instead of drifting
		// apart for good.
		constexpr std::int32_t MaxBacklogFrames = 12;
#else
		// Decoding is cheap here, but a single long frame (a window drag, a driver compiling shaders) would
		// with a one-decode cap delay the video against the already-playing music for the rest of playback.
		// A small bounded catch-up recovers from such a hitch within a few frames, while a sustained decode
		// backlog still cannot snowball into an unbounded burst.
		constexpr std::int32_t MaxDecodesPerFrame = 3;
		constexpr std::int32_t MaxBacklogFrames = MaxDecodesPerFrame;
#endif
		if (_frameProgress > _frameDelay * MaxBacklogFrames) {
			_frameProgress = _frameDelay * MaxBacklogFrames;
		}

		bool frameDecoded = false;
		for (std::int32_t i = 0; i < MaxDecodesPerFrame && _framesLeft > 0 && _frameProgress >= _frameDelay; i++) {
			_frameProgress -= _frameDelay;
			_framesLeft--;
			PrepareNextFrame();
			frameDecoded = true;
			if DEATH_UNLIKELY(_decodingFailed) {
				// The compressed streams ended prematurely (truncated or corrupted file), finish playback
				// instead of looping on a dead stream
				_framesLeft = 0;
			}
		}

		if (!frameDecoded && _nativeFormat) {
			// A frame that decodes nothing would only wait for the display, so that is when the next piece of
			// the file is read - unless a reader thread does that already (see VideoReader)
			_videoReader.Pump();
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
		Vector2i viewSize = Rendering::UpscaleRenderPass::CalculateViewSize(width, height, DefaultWidth, DefaultHeight);
		std::int32_t w = viewSize.X;
		std::int32_t h = viewSize.Y;

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

#if defined(DEATH_TARGET_N64)
		// The content prepared for this console carries the cinematics as full-motion video, decoded with the RSP's
		// help, with the music and the sound effects already mixed into its audio track - so neither the original
		// video, nor its music, nor its effects are loaded here at all
		_fullMotionVideoPath = Backends::N64FullMotionVideo::FindVideo(path);
		if (!_fullMotionVideoPath.empty()) {
			_framesLeft = 1;
			return;
		}
#endif

		if (!LoadCinematicsFromFile(path)) {
			_framesLeft = 0;
			return;
		}

#if defined(WITH_AUDIO) && (defined(WITH_OPENMPT) || defined(WITH_XMP) || defined(WITH_N64AUDIO))
		_music = resolver.GetMusic(String(path + ".j2b"_s));
#endif

		if (_nativeFormat) {
			// The first pieces of the video are read only now, after the sound effects and the music: they are
			// the last thing read before playback, so the drive is still right where the read-ahead continues,
			// and the first frames never wait for it. And it still happens before the music starts, so the time
			// it takes does not put the picture behind the music from the very first frame.
			if (!_videoReader.Start(VideoPrefillBytes / VideoReadPieceSize, VideoReadInBackground, _lz4Payloads)) {
				LOGE("Cannot load \"{}.j2v\" - Cannot read the first frames", path);
				_framesLeft = 0;
				return;
			}
		}

#if defined(WITH_AUDIO) && (defined(WITH_OPENMPT) || defined(WITH_XMP) || defined(WITH_N64AUDIO))
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
		String contentPath = fs::CombinePath("Cinematics"_s, String(path + ".j2v"_s));
		String alternativePath;
		auto s = resolver.OpenContentFile(contentPath, 64 * 1024);
		if (!s->IsValid()) {
			alternativePath = fs::FindPathCaseInsensitive(fs::CombinePath(resolver.GetSourcePath(), String(path + ".j2v"_s)));
			if (!alternativePath.empty()) {
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
			return LoadLegacyVideo(Death::move(s), path);
		}

		std::uint64_t signature = 0;
		std::memcpy(&signature, internalBuffer, sizeof(signature));
		if (AsLE(signature) == VideoFormat::Signature && internalBuffer[8] == ContentFileType::Video) {
			if (VideoReadPieceSize < 64 * 1024) {
				// The read-ahead reads pieces smaller than the buffer the file was opened with, and a read smaller
				// than the buffer goes through it - which would turn every few pieces into one read of the whole
				// buffer, exactly the long stall the small pieces are there to avoid. Opened again with a buffer
				// no larger than a piece, every piece goes to the drive as it is.
				auto reopened = (alternativePath.empty()
					? resolver.OpenContentFile(contentPath, std::int32_t(VideoReadPieceSize))
					: fs::Open(alternativePath, FileAccess::Read, std::int32_t(VideoReadPieceSize)));
				if (reopened->IsValid()) {
					s = Death::move(reopened);
				}
			}
			s->Seek(9, SeekOrigin::Begin);
			return LoadNativeVideo(Death::move(s), path);
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
		//
		// On the FIXED-PANEL consoles the drawable bounds the frame for a second, independent reason: the
		// video is aspect-fit into the panel, so a frame larger than the panel is one the hardware samples
		// back down when it draws it. Every texel past that size is one the CPU picked out of the decoded
		// frame and uploaded for something that cannot be displayed. A 640x480 video on a 320x240 console is
		// four times the texels the panel can show, which on a 93 MHz CPU is most of the cost of playing it;
		// the same applies to a handheld at 480x272. Only a starting downscale is refined here, never undone,
		// so a platform that already asked for one (the Dreamcast above) keeps it.
		//
		// Only where the panel is pinned, though: on a desktop the drawable is just the current window (the
		// default one is 720x405, SMALLER than a 640x480 video), the window can grow at any moment, and the
		// downscale decided here would outlive it - so the desktop keeps decoding at full resolution and only
		// the texture-size limit applies there.
		const std::int32_t maxTextureSize = theServiceLocator().GetRhiCapabilities()
			.GetValue(RHI::IRhiCapabilities::IntValues::MaxTextureSize);

		std::uint32_t widthLimit = 0, heightLimit = 0;	// Zero being "not limited by this"
		if (maxTextureSize > 0) {
			widthLimit = heightLimit = (std::uint32_t)maxTextureSize;
		}
#if defined(DEATH_TARGET_SWITCH) || defined(DEATH_TARGET_N64) || defined(DEATH_TARGET_WII) || \
		defined(DEATH_TARGET_GAMECUBE) || defined(DEATH_TARGET_3DS) || defined(DEATH_TARGET_DREAMCAST) || defined(DEATH_TARGET_PS2) || \
		defined(DEATH_TARGET_PS3) || defined(DEATH_TARGET_PSP) || defined(DEATH_TARGET_VITA)
		const Vector2i drawableSize = theApplication().GetResolution();
		if (drawableSize.X > 0 && drawableSize.Y > 0) {
			widthLimit = (widthLimit > 0 ? std::min(widthLimit, (std::uint32_t)drawableSize.X) : (std::uint32_t)drawableSize.X);
			heightLimit = (heightLimit > 0 ? std::min(heightLimit, (std::uint32_t)drawableSize.Y) : (std::uint32_t)drawableSize.Y);
		}
#endif

		if (widthLimit > 0 && heightLimit > 0) {
			while ((_width / _videoDownscale) > widthLimit || (_height / _videoDownscale) > heightLimit) {
				_videoDownscale *= 2;
			}
			if (_videoDownscale != downscale) {
				LOGI("Video {}x{} exceeds the {}x{} it can be drawn at (maximum texture size {}), playing it downscaled {}x",
					_width, _height, widthLimit, heightLimit, maxTextureSize, _videoDownscale);
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
			// Skip first two bytes (zlib header 0x78 0xDA). The streams were flushed per chunk and never
			// finished, so most of them carry no end-of-stream marker - running out of chunks is their
			// regular end (DeflateStream treats it as EOF) and the read-ahead below is expected to hit it.
			// Short reads still fail the decode (see Cinematics::Read).
			_compressedStreams[i].Initialize(&_fileWindow, Death::move(chunks[i]), 2);
			_decompressedStreams[i].Open(_compressedStreams[i]);
			_streamBuffers[i].Initialize(&_decompressedStreams[i]);
		}

		_videoFile = Death::move(s);

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

		// LZ4-compressed payloads are only decoded where the build carries the decoder anyway (the consoles that
		// read their content off a disc or a cartridge, see NCINE_WITH_LZ4)
#if defined(WITH_LZ4)
		const bool codecSupported = (codec == VideoFormat::CodecDeltaRle || codec == VideoFormat::CodecDeltaRleLz4);
#else
		const bool codecSupported = (codec == VideoFormat::CodecDeltaRle);
#endif
		if (pixelFormat != VideoFormat::PixelFormatIndexed8 || !codecSupported) {
			LOGE("Cannot load \"{}.j2v\" - Unsupported pixel format {} or codec {}", path, pixelFormat, codec);
			return false;
		}
		_lz4Payloads = (codec == VideoFormat::CodecDeltaRleLz4);
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

		// A video the AssetPacker made smaller than the screen it is shown on (see J2vRecompressor) stands for
		// the full-size picture, and stretching it with point sampling would show its pixels as blocks of
		// uneven size - 320x240 is drawn about 1.87 times larger on the PlayStation 2. It is filtered instead,
		// which only works where the texels are colors by the time they are filtered: a converted frame, or
		// hardware that resolves the palette as part of the texture read (filtering the INDICES, which is what
		// a palette applied afterwards would get, mixes unrelated colors). A frame shown at its own size
		// keeps point sampling, which is exact there.
		SamplerFilter frameFilter = SamplerFilter::Nearest;
#if defined(RHI_CAP_PALETTED_TEXTURES)
		constexpr bool filteredAfterPalette = true;
#else
		const bool filteredAfterPalette = _convertTo565;
#endif
		const Vector2i drawableSize = theApplication().GetResolution();
		if (filteredAfterPalette && std::int32_t(_textureWidth) < drawableSize.X && std::int32_t(_textureHeight) < drawableSize.Y) {
			frameFilter = SamplerFilter::Linear;
		}

		_textures[0] = std::make_unique<Texture>("Cinematics", textureFormat, _textureWidth, _textureHeight);
		_textures[1] = std::make_unique<Texture>("Cinematics", textureFormat, _textureWidth, _textureHeight);
		_textures[0]->SetMinFiltering(frameFilter);
		_textures[0]->SetMagFiltering(frameFilter);
		_textures[1]->SetMinFiltering(frameFilter);
		_textures[1]->SetMagFiltering(frameFilter);
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
		// The converted frame is kept current by the decoder itself (see _incremental565), which this format
		// allows because it says exactly which pixels change - and only while the frame is not downscaled
		_incremental565 = (_convertTo565 && _videoDownscale == 1);
		if (_incremental565) {
			_frame565 = std::make_unique<std::uint16_t[]>(std::size_t(_width) * _height);
		} else if (_convertTo565) {
			_frameRow = std::make_unique<std::uint16_t[]>(_textureWidth);
		}
		std::memset(_buffer.get(), 0, _width * _height);

		_framePayloadCapacity = 0;
		_decompressedPayloadCapacity = 0;
		const std::int64_t dataOffset = s->GetPosition();
		_videoFile = Death::move(s);
		// Reading starts only once everything else the video needs is loaded (see Initialize())
		if (!_videoReader.Initialize(_videoFile.get(), dataOffset, VideoReadAheadCapacity, VideoReadPieceSize)) {
			LOGE("Cannot load \"{}.j2v\" - Cannot seek in the file", path);
			return false;
		}

		LOGI("Playing cinematic \"{}.j2v\" ({}x{}, {} frames)", path, _width, _height, _framesLeft);

		LoadSfxList(path);

		return true;
	}

	void Cinematics::DecodeFrameNative()
	{
#if defined(WITH_THREADS)
		if (_videoReader.IsDecompressingAhead()) {
			// The frame has been taken out of the ring and decompressed on a thread of the reader already
			const std::uint8_t* frame;
			std::uint32_t frameSize;
			if (!_videoReader.AcquireFrame(frame, frameSize) || frameSize == 0) {
				_decodingFailed = true;
				return;
			}
			DecodePayload(frame, frame + frameSize);
			_videoReader.ReleaseFrame();
			return;
		}
#endif

		// One length-prefixed payload per frame, served out of the read-ahead ring
		bool endOfFile = false;
		std::uint8_t sizeBytes[4];
		if (!_videoReader.ReadInto(sizeBytes, sizeof(sizeBytes))) {
			_decodingFailed = true;
			return;
		}
		std::uint32_t payloadSize = std::uint32_t(sizeBytes[0]) | (std::uint32_t(sizeBytes[1]) << 8)
			| (std::uint32_t(sizeBytes[2]) << 16) | (std::uint32_t(sizeBytes[3]) << 24);
		if (payloadSize == 0 || payloadSize > 8 * 1024 * 1024) {
			_decodingFailed = true;
			return;
		}

		// Decoded straight out of the ring, so there is no per-frame copy at all - except for the rare payload
		// that wraps around the end of the ring (or would not fit into it at all), which is gathered first
		const std::uint8_t* payload = _videoReader.Peek(payloadSize, endOfFile);
		bool payloadInRing = (payload != nullptr);
		if (!payloadInRing) {
			if (endOfFile) {
				_decodingFailed = true;
				return;
			}
			if (payloadSize > _framePayloadCapacity) {
				_framePayload = std::make_unique<std::uint8_t[]>(payloadSize);
				_framePayloadCapacity = payloadSize;
			}
			if (!_videoReader.ReadInto(_framePayload.get(), payloadSize)) {
				_decodingFailed = true;
				return;
			}
			payload = _framePayload.get();
		}

		const std::uint8_t* DEATH_RESTRICT src = payload;
		const std::uint8_t* srcEnd = src + payloadSize;

#if defined(WITH_LZ4)
		if (_lz4Payloads) {
			if (payloadSize < 4) {
				_decodingFailed = true;
				return;
			}
			const std::uint32_t decompressedSize = std::uint32_t(src[0]) | (std::uint32_t(src[1]) << 8)
				| (std::uint32_t(src[2]) << 16) | (std::uint32_t(src[3]) << 24);
			src += 4;
			// A size of zero marks a payload that LZ4 could not make smaller, stored as it is
			if (decompressedSize > 0) {
				if (decompressedSize > 8 * 1024 * 1024) {
					_decodingFailed = true;
					return;
				}
				if (decompressedSize > _decompressedPayloadCapacity) {
					_decompressedPayload = std::make_unique<std::uint8_t[]>(decompressedSize);
					_decompressedPayloadCapacity = decompressedSize;
				}
#if defined(DEATH_TARGET_DREAMCAST)
				const std::int32_t decompressed = DecompressLz4Block(src, payloadSize - 4, _decompressedPayload.get(), decompressedSize);
#else
				const std::int32_t decompressed = LZ4_decompress_safe(reinterpret_cast<const char*>(src),
					reinterpret_cast<char*>(_decompressedPayload.get()), std::int32_t(payloadSize - 4), std::int32_t(decompressedSize));
#endif
				if (decompressed != std::int32_t(decompressedSize)) {
					_decodingFailed = true;
					return;
				}
				if (payloadInRing) {
					// Everything from here on reads the decompressed copy, so the ring can be refilled right away
					_videoReader.Consume(payloadSize);
					payloadInRing = false;
				}
				src = _decompressedPayload.get();
				srcEnd = src + decompressedSize;
			}
		}
#endif

		DecodePayload(src, srcEnd);

		if (payloadInRing) {
			// Only now, so the reader cannot refill the part of the ring the payload was decoded from
			_videoReader.Consume(payloadSize);
		}
	}

	void Cinematics::DecodePayload(const std::uint8_t* src, const std::uint8_t* srcEnd)
	{
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

		// Skipped spans need no work at all: the buffer still holds the previous frame. The converted frame is
		// patched along with the indices, except when a new palette arrives with this one - then every pixel
		// changes its color - or when the frame replaces most of the picture (a scene cut), where one pass
		// over the whole decoded frame afterwards costs no more than following hundreds of short spans
		// (see ApplyPaletteAndUpload).
		const std::size_t frameSize = std::size_t(_width) * _height;
		if (_incremental565 && !_paletteDirty && std::size_t(srcEnd - src) <= frameSize / 2) {
			DecodeCommands<true>(src, srcEnd, _buffer.get(), frameSize, _frame565.get(), _palette565);
		} else {
			DecodeCommands<false>(src, srcEnd, _buffer.get(), frameSize, nullptr, nullptr);
			_frame565Stale = _incremental565;
		}
	}

	bool Cinematics::LoadSfxList(StringView path)
	{
#if defined(WITH_AUDIO)
		auto& resolver = ContentResolver::Get();
		auto s = resolver.OpenContentFile(fs::CombinePath("Cinematics"_s, String(path + ".j2sfx"_s)));
		if (!s->IsValid()) {
			// Not an error - the video plays fine without its sound effects - but on a console, where the
			// content tree is prepared ahead of time and nothing else would say so, a silent intro is the
			// only symptom of a tree that was converted without "Data.j2d", and this is what tells them apart
			LOGW("SFX playlist for \"{}.j2v\" not found, the video will play without sound effects", path);
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
			if (stringSize == 0) {
				_sfxSamples.emplace_back(); // Sample not found
				continue;
			}

			String samplePath = String(NoInit, stringSize);
			s->Read(samplePath.data(), stringSize);

			String samplePathNormalized = fs::ToNativeSeparators(samplePath);
#	if defined(NCINE_HAS_NATIVE_AUDIO)
			// The console only plays its own copy of the sample, which streams from the cartridge
			String nativePath = resolver.GetNativeSoundPath(samplePathNormalized);
			if (!nativePath.empty()) {
				_sfxSamples.emplace_back(nativePath);
			} else {
				_sfxSamples.emplace_back(); // Sample not found
			}
#	else
			String fullPath = fs::CombinePath("Animations"_s, samplePathNormalized);
			auto sample = resolver.OpenContentFile(fullPath);
			if (sample->IsValid()) {
				_sfxSamples.emplace_back(Death::move(sample), fullPath);
			} else {
				_sfxSamples.emplace_back(); // Sample not found
			}
#	endif
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

		// This frame is encoded as changes against the previous one, so the previous one has to stay
		// readable while this one is written. The two buffers ping-pong for that, rather than the finished
		// frame being copied over the reference at the end of every frame: at 640x480 that copy moved
		// 300 KB a frame in each direction and measured 30 ms of a 120 ms frame on the Nintendo 64, all of
		// it spent duplicating data that was already where it needed to be. After the swap `_lastBuffer`
		// is the frame just shown and `_buffer` is the one before that, which is about to be overwritten.
		_buffer.swap(_lastBuffer);

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
				// here forever - bail out as soon as the short read is detected. Everything not yet
				// written this frame is then two frames old (the swap above), not one - acceptable,
				// because a failed decode ends the playback anyway
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
					// A back-reference the guard above rejected (corrupt frame) leaves this span holding
					// pixels from two frames ago rather than one - a well-formed stream never gets here
					x += u;
				}
			}

			// A row the stream did not describe to its end keeps the previous frame's pixels there. That
			// used to happen by itself, when the buffer being written already held a copy of that frame;
			// now that it holds the frame before it, the untouched tail is taken from the reference.
			if (x < rowWidth) {
				std::memcpy(&row[x], &_lastBuffer[std::size_t(y) * rowWidth + x], std::size_t(rowWidth - x));
			}
		}
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

		if (_frame565Stale) {
			// The decoder did not patch the converted frame this time (see DecodeFrameNative)
			_frame565Stale = false;
			ConvertIndicesTo565(indices, _frame565.get(), _palette565, _width * _height);
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
			//
			// Where the decoder keeps the converted frame current already (_incremental565), the rows are
			// copied out of it as they are, and the conversion disappears from here entirely.
			std::int32_t strideBytes = 0;
			if (std::uint8_t* mapped = static_cast<std::uint8_t*>(_textures[_textureIndex]->MapStreamingTexels(strideBytes))) {
				const std::size_t rowBytes = std::size_t(_textureWidth) * 2;
				std::uint16_t* DEATH_RESTRICT row = (_incremental565 ? _frame565.get() : _frameRow.get());
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
						if (_incremental565) {
							row = &_frame565[std::size_t(y) * _textureWidth];
						} else {
							ConvertIndicesTo565(&texels[std::size_t(y) * _textureWidth], row, _palette565, _textureWidth);
						}
						sq_fast_cpy(SQ_MASK_DEST(mapped + std::size_t(y) * strideBytes), row, rowBytes / 32);
					}
					sq_unlock();
					return;
				}
#endif
				for (std::uint32_t y = 0; y < _textureHeight; y++) {
					if (_incremental565) {
						row = &_frame565[std::size_t(y) * _textureWidth];
					} else {
						ConvertIndicesTo565(&texels[std::size_t(y) * _textureWidth], row, _palette565, _textureWidth);
					}
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
			// The whole-frame buffer is only needed by this fallback (or kept current by the decoder already),
			// so it is allocated if it is ever reached
			if (!_incremental565) {
				if (_frame565 == nullptr) {
					_frame565 = std::make_unique<std::uint16_t[]>(std::size_t(_textureWidth) * _textureHeight);
				}
				ConvertIndicesTo565(texels, _frame565.get(), _palette565, _textureWidth * _textureHeight);
			}
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
		_chunks = Death::move(chunks);
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

	Cinematics::VideoReader::VideoReader()
		: _file(nullptr), _data(nullptr), _capacity(0), _pieceSize(0), _readPos(0), _writePos(0), _filled(0), _skip(0),
			_fileOffset(0), _endOfFile(false), _background(false), _decompressAhead(false)
#if defined(WITH_THREADS)
			, _stopRequested(false), _firstReadySlot(0), _readySlots(0), _framesEnded(false), _recordCapacity(0)
#endif
	{
	}

	Cinematics::VideoReader::~VideoReader()
	{
		Shutdown();
	}

	bool Cinematics::VideoReader::Initialize(Stream* file, std::int64_t dataOffset, std::uint32_t capacity, std::uint32_t pieceSize)
	{
		Shutdown();

		_file = file;
		_pieceSize = pieceSize;
		// A whole number of pieces, so a piece never has to be split at the end of the ring - which keeps every
		// read the same size, and both its destination and its offset in the file aligned for DMA
		_capacity = std::max(capacity / pieceSize, std::uint32_t(2)) * pieceSize;
		if (_allocation == nullptr) {
			// Over-allocated so the ring itself can start on an aligned address
			_allocation = std::make_unique<std::uint8_t[]>(_capacity + Alignment);
		}
		_data = reinterpret_cast<std::uint8_t*>((reinterpret_cast<std::uintptr_t>(_allocation.get()) + Alignment - 1)
			& ~std::uintptr_t(Alignment - 1));
		_readPos = 0;
		_writePos = 0;
		_filled = 0;
		_endOfFile = false;
		_background = false;
		_decompressAhead = false;

		// The Dreamcast's disc driver only transfers by DMA when both the destination and the position within
		// the file are 32 byte aligned (it checks `ptr & 31`), and falls back to PIO otherwise - which is slower
		// by orders of magnitude and turns each read into a multi-second stall. So reading starts at an aligned
		// offset, the bytes before the first frame are skipped, and every piece keeps that alignment.
		_fileOffset = dataOffset & ~std::int64_t(Alignment - 1);
		_skip = std::uint32_t(dataOffset - _fileOffset);
		return (_file->Seek(_fileOffset, SeekOrigin::Begin) == _fileOffset);
	}

	bool Cinematics::VideoReader::Start(std::uint32_t prefillPieces, bool background, bool decompressAhead)
	{
		for (std::uint32_t i = 0; i < prefillPieces && !_endOfFile && _capacity - _filled >= _pieceSize; i++) {
			ReadPiece();
		}
		if (WaitForData(_skip) < _skip) {
			return false;
		}
		Consume(_skip);
		_skip = 0;

#if defined(WITH_THREADS)
		if (background) {
			_stopRequested = false;
			_background = true;
#	if defined(WITH_LZ4)
			_decompressAhead = decompressAhead;
#	else
			static_cast<void>(decompressAhead);
#	endif
			_firstReadySlot = 0;
			_readySlots = 0;
			_framesEnded = false;
			_thread = Thread(ThreadFunc, this);
#	if defined(WITH_LZ4)
			if (_decompressAhead) {
#		if defined(DEATH_TARGET_DREAMCAST)
				_mainThreadPriority = std::int32_t(thd_get_prio(thd_get_current()));
#		endif
				_decompressThread = Thread(DecompressThreadFunc, this);
			}
#	endif
		}
#else
		static_cast<void>(background);
		static_cast<void>(decompressAhead);
#endif
		return true;
	}

	void Cinematics::VideoReader::Shutdown()
	{
#if defined(WITH_THREADS)
		if (_background) {
			_mutex.Lock();
			_stopRequested = true;
			_spaceAvailable.Signal();
			_dataAvailable.Signal();
			_slotAvailable.Signal();
			_mutex.Unlock();
			if (_decompressAhead) {
				_decompressThread.Join();
			}
			_thread.Join();
			_background = false;
		}
#endif
	}

	void Cinematics::VideoReader::ReadPiece()
	{
		// Only the free part of the ring is written, which the consumer does not touch, so the read itself
		// runs without the lock - that is the whole point of having a thread for it
		std::uint8_t* destination = _data + _writePos;
		std::int64_t bytesRead = _file->Read(destination, _pieceSize);
		const std::uint32_t length = (bytesRead > 0 ? std::uint32_t(bytesRead) : 0);

#if defined(WITH_THREADS)
		if (_background) {
			_mutex.Lock();
		}
#endif
		_fileOffset += length;
		_writePos += length;
		if (_writePos >= _capacity) {
			_writePos -= _capacity;
		}
		_filled += length;
		if (length < _pieceSize) {
			// A short read is the end of the file (or a failed read, which ends playback the same way)
			_endOfFile = true;
		}
#if defined(WITH_THREADS)
		if (_background) {
			_dataAvailable.Signal();
			_mutex.Unlock();
		}
#endif
	}

	std::uint32_t Cinematics::VideoReader::WaitForData(std::uint32_t bytes)
	{
#if defined(WITH_THREADS)
		if (_background) {
			_mutex.Lock();
			while (_filled < bytes && !_endOfFile && !_stopRequested) {
				_dataAvailable.Wait(_mutex);
			}
			// Taken under the lock: everything the reader filled up to here is visible to this thread now
			const std::uint32_t available = _filled;
			_mutex.Unlock();
			return available;
		}
#endif
		// Without a thread the frame that needs the data has to wait for the drive itself - the case the
		// read-ahead in Pump() exists to make rare
		while (_filled < bytes && !_endOfFile) {
			if (_capacity - _filled < _pieceSize) {
				// The ring cannot take another piece before the consumer frees some, which it does only once
				// it has what it waits for - so the request is larger than the ring can ever hold at once
				break;
			}
			ReadPiece();
		}
		return _filled;
	}

	const std::uint8_t* Cinematics::VideoReader::Peek(std::uint32_t bytes, bool& endOfFile)
	{
		endOfFile = false;
		if (bytes > _capacity - _pieceSize || _readPos + bytes > _capacity) {
			// Wraps around the end of the ring, or could never be held at once (with a reader thread, a
			// request larger than the ring minus one piece would wait for a piece that has no room)
			return nullptr;
		}
		if (WaitForData(bytes) < bytes) {
			endOfFile = true;
			return nullptr;
		}
		return _data + _readPos;
	}

	bool Cinematics::VideoReader::ReadInto(std::uint8_t* destination, std::uint32_t bytes)
	{
		// Gathered piece by piece and released as it goes, so this works for any size, also one larger than
		// the ring
		while (bytes > 0) {
			const std::uint32_t available = WaitForData(std::min(bytes, _pieceSize));
			if (available == 0) {
				return false;
			}
			std::uint32_t chunk = std::min(bytes, available);
			chunk = std::min(chunk, _capacity - _readPos);
			std::memcpy(destination, _data + _readPos, chunk);
			Consume(chunk);
			destination += chunk;
			bytes -= chunk;
		}
		return true;
	}

	void Cinematics::VideoReader::Consume(std::uint32_t bytes)
	{
#if defined(WITH_THREADS)
		if (_background) {
			_mutex.Lock();
		}
#endif
		_readPos += bytes;
		if (_readPos >= _capacity) {
			_readPos -= _capacity;
		}
		_filled -= bytes;
#if defined(WITH_THREADS)
		if (_background) {
			_spaceAvailable.Signal();
			_mutex.Unlock();
		}
#endif
	}

	void Cinematics::VideoReader::Pump()
	{
		if (!_background && !_endOfFile && _file != nullptr && _capacity - _filled >= _pieceSize) {
			ReadPiece();
		}
	}

#if defined(WITH_THREADS)
	bool Cinematics::VideoReader::AcquireFrame(const std::uint8_t*& payload, std::uint32_t& size)
	{
		_mutex.Lock();
		while (_readySlots == 0 && !_framesEnded) {
			_frameAvailable.Wait(_mutex);
		}
		const bool available = (_readySlots > 0);
		FrameSlot& slot = _frameSlots[_firstReadySlot];
		_mutex.Unlock();
		if (!available) {
			return false;
		}
		// The decompressing thread never writes a ready slot, so it is read without the lock
		payload = slot.Data.get();
		size = slot.Size;
		return true;
	}

	void Cinematics::VideoReader::ReleaseFrame()
	{
		_mutex.Lock();
		_firstReadySlot = (_firstReadySlot + 1) % FrameSlotCount;
		_readySlots--;
		_slotAvailable.Signal();
		_mutex.Unlock();
	}

#	if defined(WITH_LZ4)
	bool Cinematics::VideoReader::DecompressNextFrame(FrameSlot& slot)
	{
		// The same steps the main thread takes in DecodeFrameNative() otherwise
		std::uint8_t sizeBytes[4];
		if (!ReadInto(sizeBytes, sizeof(sizeBytes))) {
			return false;
		}
		const std::uint32_t recordSize = std::uint32_t(sizeBytes[0]) | (std::uint32_t(sizeBytes[1]) << 8)
			| (std::uint32_t(sizeBytes[2]) << 16) | (std::uint32_t(sizeBytes[3]) << 24);
		if (recordSize < 4 || recordSize > 8 * 1024 * 1024) {
			return false;
		}
		bool endOfFile = false;
		const std::uint8_t* src = Peek(recordSize, endOfFile);
		const bool recordInRing = (src != nullptr);
		if (!recordInRing) {
			if (endOfFile) {
				return false;
			}
			if (recordSize > _recordCapacity) {
				_record = std::make_unique<std::uint8_t[]>(recordSize);
				_recordCapacity = recordSize;
			}
			if (!ReadInto(_record.get(), recordSize)) {
				return false;
			}
			src = _record.get();
		}

		const std::uint32_t decompressedSize = std::uint32_t(src[0]) | (std::uint32_t(src[1]) << 8)
			| (std::uint32_t(src[2]) << 16) | (std::uint32_t(src[3]) << 24);
		// A size of zero marks a payload that LZ4 could not make smaller, stored as it is
		const std::uint32_t size = (decompressedSize > 0 ? decompressedSize : recordSize - 4);
		bool decoded = (size <= 8 * 1024 * 1024);
		if (decoded) {
			if (size > slot.Capacity) {
				slot.Data = std::make_unique<std::uint8_t[]>(size);
				slot.Capacity = size;
			}
			if (decompressedSize == 0) {
				std::memcpy(slot.Data.get(), src + 4, size);
			} else {
#		if defined(DEATH_TARGET_DREAMCAST)
				const std::int32_t decompressed = DecompressLz4Block(src + 4, recordSize - 4, slot.Data.get(), size);
#		else
				const std::int32_t decompressed = LZ4_decompress_safe(reinterpret_cast<const char*>(src + 4),
					reinterpret_cast<char*>(slot.Data.get()), std::int32_t(recordSize - 4), std::int32_t(size));
#		endif
				decoded = (decompressed == std::int32_t(size));
			}
			slot.Size = size;
		}
		if (recordInRing) {
			Consume(recordSize);
		}
		return decoded;
	}

	void Cinematics::VideoReader::DecompressThreadFunc(void* arg)
	{
		Thread::SetCurrentName("Cinematics decompressor");

		VideoReader* reader = static_cast<VideoReader*>(arg);
#		if defined(DEATH_TARGET_DREAMCAST)
		// KallistiOS shares the processor between threads of one priority in timeslices, so this thread would take
		// turns with the main thread in the middle of its frames. One step below it, it only runs while the main
		// thread waits - for the display, or for a frame that is not decompressed yet. (A larger number is a lower
		// priority there.)
		thd_set_prio(thd_get_current(), prio_t(reader->_mainThreadPriority + 1));
#		endif

		reader->_mutex.Lock();
		while (!reader->_stopRequested && !reader->_framesEnded) {
			if (reader->_readySlots == FrameSlotCount) {
				reader->_slotAvailable.Wait(reader->_mutex);
				continue;
			}
			FrameSlot& slot = reader->_frameSlots[(reader->_firstReadySlot + reader->_readySlots) % FrameSlotCount];
			reader->_mutex.Unlock();
			const bool decompressed = reader->DecompressNextFrame(slot);
			reader->_mutex.Lock();
			if (decompressed) {
				reader->_readySlots++;
			} else {
				reader->_framesEnded = true;
			}
			reader->_frameAvailable.Signal();
		}
		reader->_mutex.Unlock();
	}
#	endif

	void Cinematics::VideoReader::ThreadFunc(void* arg)
	{
		Thread::SetCurrentName("Cinematics reader");

		VideoReader* reader = static_cast<VideoReader*>(arg);
		reader->_mutex.Lock();
		while (!reader->_stopRequested && !reader->_endOfFile) {
			if (reader->_capacity - reader->_filled < reader->_pieceSize) {
				reader->_spaceAvailable.Wait(reader->_mutex);
				continue;
			}
			reader->_mutex.Unlock();
			reader->ReadPiece();
			reader->_mutex.Lock();
		}
		reader->_mutex.Unlock();
	}
#endif

#if defined(WITH_AUDIO)
	Cinematics::SfxItem::SfxItem()
	{
	}

#	if !defined(NCINE_HAS_NATIVE_AUDIO)
	Cinematics::SfxItem::SfxItem(std::unique_ptr<Stream> stream, StringView path)
		: Buffer(std::make_unique<AudioBuffer>(Death::move(stream), path))
	{
	}
#	endif

	Cinematics::SfxItem::SfxItem(StringView path)
		: Buffer(std::make_unique<AudioBuffer>(path))
	{
	}
#endif
}