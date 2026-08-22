#include "RdpTexture.h"
#include "RdpDevice.h"

#include "../../../../Main.h"

#include <cstring>
#include <malloc.h>

#include <n64sys.h>
#include <rspq.h>

using namespace Death::Containers::Literals;

namespace nCine::RHI::RDP
{
	namespace
	{
		// The RDP DMAs texel rows straight out of RDRAM, so a store row must be a whole number of
		// 64-bit words and the base a whole cache line (the writebacks below flush exactly the store)
		constexpr std::int32_t RowAlignment = 8;
		constexpr std::int32_t BaseAlignment = 64;

		// "Never sampled" sentinel of the used-in-frame stamps
		constexpr std::uint32_t NeverSampled = ~std::uint32_t(0);

		// Waits for the stamped frame's syncpoint when it says in-flight commands could still DMA the
		// store that is about to be rewritten or freed (an exact check against what the RDP has actually
		// retired - see RdpDevice::IsFrameRetired). The steady-state writers never pay this - the
		// cinematics alternate two textures, the bake cache holds a frame's rows - it only fires on
		// transitions (level loads, a texture resized while on screen).
		inline void WaitIfInFlight(std::uint32_t lastUsedFrame)
		{
			if (lastUsedFrame != NeverSampled && RdpDevice::IsInitialized() &&
				!RdpDevice::IsFrameRetired(lastUsedFrame)) {
				RdpDevice::WaitForFrame(lastUsedFrame);
			}
		}

		inline std::int32_t AlignRow(std::int32_t bytes)
		{
			return (bytes + RowAlignment - 1) & ~(RowAlignment - 1);
		}

	}

	std::uint32_t RdpTexture::_nextHandle = 1;
	std::uint32_t RdpTexture::_nextContentVersion = 0;

	RdpTexture::RdpTexture(TextureTarget target)
		: _handle(_nextHandle++), _contentVersion(0), _target(target), _format(PixelFormat::Unknown), _uploadFormat(PixelFormat::Unknown),
			_width(0), _height(0), _strideBytes(0), _bytesPerPixel(0),
			_minFilter(nCine::SamplerFilter::Nearest), _magFilter(nCine::SamplerFilter::Nearest), _wrap(SamplerWrapping::ClampToEdge),
			_textureUnit(0), _pixels(nullptr), _pixelsSize(0), _isRenderTarget(false), _isPaletteTexture(false),
			_store(nullptr), _storeSize(0), _storeStride(0), _texFormat(FMT_NONE), _surface{}, _storeValid(false),
			_streamingWritebackPending(false), _bakedStores{}, _nextBakedStore(0), _activeBakeStamp(0), _lastSampledFrame(NeverSampled)
	{
		_swizzle[0] = SwizzleChannel::Red;
		_swizzle[1] = SwizzleChannel::Green;
		_swizzle[2] = SwizzleChannel::Blue;
		_swizzle[3] = SwizzleChannel::Alpha;
	}

	RdpTexture::~RdpTexture()
	{
		RdpDevice::UnbindTexture(this);
		FreeStores();
		FreePixels();
	}

	std::int32_t RdpTexture::BytesPerPixel(PixelFormat format)
	{
		// The one authoritative table lives next to the PixelFormat enum itself; the formats this
		// backend accepts are gated by Allocate()'s own switch, not by this size
		return nCine::BytesPerPixel(format);
	}

	const std::uint8_t* RdpTexture::GetPixels(std::int32_t level) const
	{
		static_cast<void>(level);
		if (_uploadFormat == PixelFormat::R8) {
			// The R8 store IS the linear host image (identity bytes, padded stride)
			return _store;
		}
		return _pixels;
	}

	std::uint8_t* RdpTexture::MutablePixels()
	{
		if (_uploadFormat == PixelFormat::R8) {
			return _store;
		}
		return _pixels;
	}

	bool RdpTexture::AllocatePixels(std::size_t size)
	{
		FreePixels();
		if (size == 0) {
			return true;
		}
		_pixels = static_cast<std::uint8_t*>(malloc(size));
		if (_pixels == nullptr) {
			LOGE("Out of memory allocating a {}x{} texture host copy ({} bytes)", _width, _height, size);
			return false;
		}
		_pixelsSize = size;
		std::memset(_pixels, 0, size);
		return true;
	}

	void RdpTexture::FreePixels()
	{
		if (_pixels != nullptr) {
			free(_pixels);
			_pixels = nullptr;
		}
		_pixelsSize = 0;
	}

	void RdpTexture::FreeStores()
	{
		// Freed memory must not stay referenced by in-flight commands (a texture destroyed or resized
		// right after present); the stamp keeps this free for stores no recent frame sampled. A render
		// target's surface is WRITTEN by in-flight draws rather than sampled, which no stamp tracks, so
		// it always drains - render targets are few and die at teardown, never per frame.
		if (_isRenderTarget && _store != nullptr && RdpDevice::IsInitialized()) {
			rspq_wait();
		}
		WaitIfInFlight(_lastSampledFrame);
		_lastSampledFrame = NeverSampled;
		if (_store != nullptr) {
			free(_store);
			_store = nullptr;
		}
		_storeSize = 0;
		_storeValid = false;
		for (std::int32_t i = 0; i < BakedStoreCount; i++) {
			if (_bakedStores[i].Data != nullptr) {
				free(_bakedStores[i].Data);
				_bakedStores[i].Data = nullptr;
			}
			_bakedStores[i].Valid = false;
		}
	}

	void RdpTexture::Allocate(PixelFormat format, std::int32_t width, std::int32_t height)
	{
		if (format == _format && width == _width && height == _height) {
			return;
		}
		FreeStores();

		_format = format;
		_uploadFormat = format;
		_width = width;
		_height = height;
		_bytesPerPixel = BytesPerPixel(format);
		FreePixels();

		switch (format) {
			case PixelFormat::R8:
				// The CI8 store doubles as the host image, so it exists (zeroed) from allocation on -
				// TexStorage2D-then-TexSubImage2D is the common upload order
				_storeStride = AlignRow(width);
				_strideBytes = _storeStride;
				_texFormat = FMT_CI8;
				EnsureStore();
				if (_store != nullptr) {
					std::memset(_store, 0, _storeSize);
					data_cache_hit_writeback(_store, _storeSize);
					_storeValid = true;
				}
				break;
			case PixelFormat::RG8:
				// Index + per-pixel alpha: the linear host copy feeds the per-palette-row bakes, so it
				// lives as long as the texture does
				_strideBytes = width * _bytesPerPixel;
				_storeStride = AlignRow(width * 2);
				_texFormat = FMT_RGBA16;
				AllocatePixels(std::size_t(RawPixelsSize()));
				break;
			case PixelFormat::RGBA8:
			case PixelFormat::RGB8:
			case PixelFormat::RGB565:
			case PixelFormat::RGB5A1:
			case PixelFormat::RGBA4:
				// Direct color converts to RGBA5551 at the first draw that samples it, and the host copy
				// is released there - it only has to outlive the conversion, plus the palette case that
				// never reaches one (see the class documentation). A render target is written by the RDP
				// rather than uploaded, so it never needs one at all; the flag is already set when a
				// target is resized through TexStorage2D, and the first-time path releases it in
				// SetRenderTarget() below.
				_strideBytes = width * _bytesPerPixel;
				_storeStride = AlignRow(width * 2);
				_texFormat = FMT_RGBA16;
				if (!_isRenderTarget) {
					AllocatePixels(std::size_t(RawPixelsSize()));
				}
				break;
			default:
				LOGW("Unsupported texture format {}", std::int32_t(format));
				_strideBytes = 0;
				_storeStride = 0;
				_texFormat = FMT_NONE;
				break;
		}
	}

	bool RdpTexture::EnsureStore()
	{
		if (_width <= 0 || _height <= 0 || _storeStride <= 0 || _texFormat == FMT_NONE) {
			return false;
		}
		const std::size_t size = std::size_t(_storeStride) * std::size_t(_height);
		if (_store != nullptr && _storeSize >= size) {
			return true;
		}
		if (_store != nullptr) {
			WaitIfInFlight(_lastSampledFrame);
			free(_store);
			_storeValid = false;
		}
		_store = static_cast<std::uint8_t*>(memalign(BaseAlignment, size));
		_storeSize = (_store != nullptr ? size : 0);
		if (_store == nullptr) {
			LOGE("Out of memory allocating a {}x{} texture store ({} bytes)", _width, _height, size);
			return false;
		}
		_surface = surface_make(_store, tex_format_t(_texFormat), std::uint16_t(_width), std::uint16_t(_height), std::uint16_t(_storeStride));
		return true;
	}

	std::uint16_t RdpTexture::PackStoreTexel(const std::uint8_t* texel) const
	{
		// The same conversions the per-format loops of RefreshStore() below do, one texel at a time - see
		// the comments there for why texel channels are read positionally
		switch (_uploadFormat) {
			case PixelFormat::RGBA8:
				return Pack5551(texel[0], texel[1], texel[2], texel[3]);
			case PixelFormat::RGB8:
				return Pack5551(texel[0], texel[1], texel[2], 255);
			case PixelFormat::RGB565: {
				// Assembled from the bytes rather than loaded as a halfword: a sub-upload's source rows
				// carry no alignment guarantee, and on this big-endian target the two are the same value
				const std::uint16_t v = std::uint16_t((texel[0] << 8) | texel[1]);
				// 565's blue sits at bits 4:0 and moves up to 5551's 5:1 whole; only green drops its LSB
				return std::uint16_t(((v >> 11) << 11) | (((v >> 6) & 0x1F) << 6) | ((v & 0x1F) << 1) | 1);
			}
			case PixelFormat::RGB5A1:
				return std::uint16_t((texel[0] << 8) | texel[1]);
			case PixelFormat::RGBA4: {
				const std::uint16_t v = std::uint16_t((texel[0] << 8) | texel[1]);
				return Pack5551(std::uint8_t(((v >> 12) & 0xF) * 17), std::uint8_t(((v >> 8) & 0xF) * 17),
					std::uint8_t(((v >> 4) & 0xF) * 17), std::uint8_t((v & 0xF) * 17));
			}
			default:
				return 0;
		}
	}

	void RdpTexture::RefreshStore()
	{
		const bool storeExisted = (_store != nullptr);
		if (!EnsureStore()) {
			return;
		}
		if (_pixels == nullptr) {
			// The host copy was released after the first conversion (below); the store is the only copy
			// from then on and sub-uploads convert straight into it, so there is nothing to reconvert
			_storeValid = storeExisted;
			return;
		}
		// The store's previous contents may still be DMAed by the in-flight frame
		WaitIfInFlight(_lastSampledFrame);

		// Convert the host image to RGBA5551. TEXEL data is a byte sequence r,g,b,a (the loaders and
		// ContentResolver write image channels byte-wise), so it is extracted positionally - exactly like
		// the GX backend's RGBA8 tiling on the other big-endian console. Only PALETTE entries are
		// value-packed uint32 words instead (see EnsureBakedStore below).
		for (std::int32_t y = 0; y < _height; y++) {
			const std::uint8_t* src = _pixels + std::size_t(y) * _strideBytes;
			std::uint16_t* dst = reinterpret_cast<std::uint16_t*>(_store + std::size_t(y) * _storeStride);
			switch (_uploadFormat) {
				case PixelFormat::RGBA8:
					for (std::int32_t x = 0; x < _width; x++) {
						dst[x] = Pack5551(src[x * 4], src[x * 4 + 1], src[x * 4 + 2], src[x * 4 + 3]);
					}
					break;
				case PixelFormat::RGB8:
					for (std::int32_t x = 0; x < _width; x++) {
						dst[x] = Pack5551(src[x * 3], src[x * 3 + 1], src[x * 3 + 2], 255);
					}
					break;
				case PixelFormat::RGB565: {
					// Engine-packed 565 values in native (big-endian) words; the green LSB is dropped
					// (blue's five bits move up whole, see PackStoreTexel)
					const std::uint16_t* src16 = reinterpret_cast<const std::uint16_t*>(src);
					for (std::int32_t x = 0; x < _width; x++) {
						const std::uint16_t v = src16[x];
						dst[x] = std::uint16_t(((v >> 11) << 11) | (((v >> 6) & 0x1F) << 6) | ((v & 0x1F) << 1) | 1);
					}
					break;
				}
				case PixelFormat::RGB5A1: {
					// Same bit layout as the hardware format
					std::memcpy(dst, src, std::size_t(_width) * 2);
					break;
				}
				case PixelFormat::RGBA4: {
					const std::uint16_t* src16 = reinterpret_cast<const std::uint16_t*>(src);
					for (std::int32_t x = 0; x < _width; x++) {
						const std::uint16_t v = src16[x];
						const std::uint8_t r = std::uint8_t(((v >> 12) & 0xF) * 17);
						const std::uint8_t g = std::uint8_t(((v >> 8) & 0xF) * 17);
						const std::uint8_t b = std::uint8_t(((v >> 4) & 0xF) * 17);
						const std::uint8_t a = std::uint8_t((v & 0xF) * 17);
						dst[x] = Pack5551(r, g, b, a);
					}
					break;
				}
				default:
					return;
			}
		}
		data_cache_hit_writeback(_store, _storeSize);
		RdpDevice::TraceStoreRebuild(std::uint32_t(_storeSize), false);
		_storeValid = true;

		// The image has reached the form the RDP samples, and getting here proves the texture is sampled
		// rather than read as a palette - the one role a host copy of a direct-color texture serves (see
		// the class documentation). Keeping it would be a second, WIDER copy of every image in the game;
		// the sub-upload path converts straight into the store from now on. RG8 is excluded because its
		// bakes reconvert the host copy through a new palette row on demand, and the palette texture
		// because it is read from here and never sampled (its AcquireSurface() returns nothing, so it
		// cannot reach this line anyway - the test is stated rather than relied upon).
		if (_uploadFormat != PixelFormat::RG8 && !_isPaletteTexture) {
			FreePixels();
		}
	}

	const surface_t* RdpTexture::AcquireSurface()
	{
		if (_isPaletteTexture) {
			return nullptr;		// Rows become TLUTs, the texture itself is never sampled
		}
		switch (_uploadFormat) {
			case PixelFormat::R8:
				if (_store == nullptr) {
					return nullptr;
				}
				if (_streamingWritebackPending) {
					// A streamed frame was written straight into the store; make it visible to the RDP
					data_cache_hit_writeback(_store, _storeSize);
					_streamingWritebackPending = false;
				}
				_lastSampledFrame = RdpDevice::GetSceneCounter();
				return &_surface;
			case PixelFormat::RG8: {
				// Only a bake selected by EnsureBakedStore() can be sampled
				for (std::int32_t i = 0; i < BakedStoreCount; i++) {
					if (_bakedStores[i].Valid && _surface.buffer == _bakedStores[i].Data && _surface.buffer != nullptr) {
						_lastSampledFrame = RdpDevice::GetSceneCounter();
						return &_surface;
					}
				}
				return nullptr;
			}
			default:
				if (_texFormat == FMT_NONE) {
					return nullptr;
				}
				if (!_storeValid) {
					RefreshStore();
				}
				if (!_storeValid) {
					return nullptr;
				}
				_lastSampledFrame = RdpDevice::GetSceneCounter();
				return &_surface;
		}
	}

	bool RdpTexture::EnsureBakedStore(const std::uint32_t* paletteRow, std::uint32_t paletteRowIndex,
		std::uint32_t paletteGeneration, const void* palette)
	{
		if (_uploadFormat != PixelFormat::RG8 || paletteRow == nullptr || _pixels == nullptr ||
			_width <= 0 || _height <= 0) {
			return false;
		}

		// A cached bake matching (row, palette generation, texel content) is switched to without a rebuild
		for (std::int32_t i = 0; i < BakedStoreCount; i++) {
			BakedStore& slot = _bakedStores[i];
			if (slot.Valid && slot.PaletteRow == paletteRowIndex && slot.ContentVersion == _contentVersion &&
				slot.Palette == palette) {
				if (slot.PaletteGeneration != paletteGeneration) {
					// The palette texture was written somewhere; only a change to THIS row invalidates the
					// bake, and re-hashing the row costs a thousandth of rebuilding it (see HashPaletteRow)
					if (slot.PaletteHash != HashPaletteRow(paletteRow)) {
						break;
					}
					slot.PaletteGeneration = paletteGeneration;
				}
				slot.LastUsedFrame = RdpDevice::GetSceneCounter();
				_activeBakeStamp = slot.BakeStamp;
				_surface = surface_make(slot.Data, FMT_RGBA16, std::uint16_t(_width), std::uint16_t(_height), std::uint16_t(_storeStride));
				return true;
			}
		}

		BakedStore& slot = _bakedStores[_nextBakedStore];
		_nextBakedStore = (_nextBakedStore + 1) % BakedStoreCount;
		if (slot.Valid) {
			// The slot's previous bake may still be DMAed by the in-flight frame (more distinct palette
			// rows across two frames than the cache holds - rare, e.g. heavy palette cycling)
			WaitIfInFlight(slot.LastUsedFrame);
		}
		const std::size_t size = std::size_t(_storeStride) * std::size_t(_height);
		if (slot.Data == nullptr) {
			slot.Data = static_cast<std::uint8_t*>(memalign(BaseAlignment, size));
			if (slot.Data == nullptr) {
				LOGE("Out of memory allocating a {}x{} baked store ({} bytes)", _width, _height, size);
				return false;
			}
		}

		// Index resolved through the palette row, alpha thresholded from the texel's own alpha byte into
		// RGBA5551's single bit. A palette entry is a uint32 whose VALUE has red in the low byte (the
		// engine-wide `color & 0xFF` convention), so the channels are extracted by value - positional
		// bytes would come out a,b,g,r on this big-endian console (the GX backend, the other big-endian
		// one, converts its bakes the same way).
		for (std::int32_t y = 0; y < _height; y++) {
			const std::uint8_t* src = _pixels + std::size_t(y) * _strideBytes;
			std::uint16_t* dst = reinterpret_cast<std::uint16_t*>(slot.Data + std::size_t(y) * _storeStride);
			for (std::int32_t x = 0; x < _width; x++) {
				const std::uint32_t rgba = paletteRow[src[x * 2]];
				dst[x] = Pack5551(std::uint8_t(rgba & 0xFF), std::uint8_t((rgba >> 8) & 0xFF),
					std::uint8_t((rgba >> 16) & 0xFF), src[x * 2 + 1]);
			}
		}
		data_cache_hit_writeback(slot.Data, size);
		RdpDevice::TraceStoreRebuild(std::uint32_t(size), true);

		slot.PaletteRow = paletteRowIndex;
		slot.PaletteGeneration = paletteGeneration;
		slot.PaletteHash = HashPaletteRow(paletteRow);
		slot.ContentVersion = _contentVersion;
		slot.LastUsedFrame = RdpDevice::GetSceneCounter();
		// A fresh stamp even though the Data pointer may be recycled: TMEM can still hold the previous
		// row's texels under the same pointer, and the device's residency dedup must see a difference
		slot.BakeStamp = ++_nextContentVersion;
		slot.Palette = palette;
		slot.Valid = true;
		_activeBakeStamp = slot.BakeStamp;
		_surface = surface_make(slot.Data, FMT_RGBA16, std::uint16_t(_width), std::uint16_t(_height), std::uint16_t(_storeStride));
		return true;
	}

	void* RdpTexture::MapStreamingTexels(std::int32_t& strideBytes)
	{
		// Only R8 is streamable: its store bytes are the palette indices themselves, so the writer's
		// layout and the RDP's agree. Direct-color stores are RGBA5551 - not the engine layout the
		// writer would produce - so those callers take the converting upload path instead.
		if (_uploadFormat != PixelFormat::R8 || _isPaletteTexture || _isRenderTarget || !EnsureStore()) {
			strideBytes = 0;
			return nullptr;
		}
		// A frame in flight may still read the store's previous contents; never fires for the cinematics,
		// which alternate two textures exactly so the write lands a frame behind the last sampling
		WaitIfInFlight(_lastSampledFrame);
		strideBytes = _storeStride;
		_streamingWritebackPending = true;
		_storeValid = true;
		_contentVersion = ++_nextContentVersion;
		return _store;
	}

	void RdpTexture::SetRenderTarget(bool isRenderTarget)
	{
		_isRenderTarget = isRenderTarget;
		if (isRenderTarget) {
			// The target surface is what rdpq_attach() renders into AND what later draws sample, so it
			// exists eagerly and there is no host copy to keep in sync
			_texFormat = FMT_RGBA16;
			_storeStride = AlignRow(_width * 2);
			// The RDP writes the surface; nothing ever reads a host copy of it (a target is not a
			// palette, and GetPixels() reports none) - so release the one Allocate() made before the
			// texture was known to be a target. A growable container's clear() kept that block alive,
			// which on the menu's 256x256 RGB8 backdrop target was 192 KB of pure duplicate.
			FreePixels();
			if (EnsureStore()) {
				WaitIfInFlight(_lastSampledFrame);
				std::memset(_store, 0, _storeSize);
				data_cache_hit_writeback(_store, _storeSize);
				_storeValid = true;
			}
		}
	}

	const surface_t* RdpTexture::GetRenderTargetSurface() const
	{
		return (_isRenderTarget && _store != nullptr ? &_surface : nullptr);
	}

	bool RdpTexture::Bind(std::uint32_t textureUnit) const
	{
		_textureUnit = textureUnit;
		RdpDevice::BindTexture(textureUnit, this);
		return true;
	}

	bool RdpTexture::Unbind() const
	{
		RdpDevice::BindTexture(_textureUnit, nullptr);
		return true;
	}

	bool RdpTexture::Unbind(std::uint32_t textureUnit)
	{
		RdpDevice::BindTexture(textureUnit, nullptr);
		return true;
	}

	void RdpTexture::TexImage2D(std::int32_t level, PixelFormat format, bool bgr, std::int32_t width, std::int32_t height, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0) {
			return;		// Level 0 only
		}
		Allocate(format, width, height);
		if (data != nullptr) {
			TexSubImage2D(0, 0, 0, width, height, format, bgr, data);
			return;
		}
		if (_isPaletteTexture) {
			RdpDevice::NotifyPaletteTextureChanged(this, 0, _height);
		}
	}

	void RdpTexture::TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0 || data == nullptr || _width <= 0 || _height <= 0) {
			return;
		}
		const std::int32_t srcBpp = BytesPerPixel(format);
		const std::int32_t dstBpp = _bytesPerPixel;
		const std::int32_t copyBpp = (srcBpp < dstBpp ? srcBpp : dstBpp);
		if (srcBpp == 0 || dstBpp == 0) {
			return;
		}
		// The patch lands in the RDP store directly for R8 (whose store IS the host image) and for a
		// direct-color texture whose host copy was released once its store was built - those convert texel
		// by texel on the way in, which is what makes the store the only copy (see RefreshStore()). An RG8
		// store is a palette bake rather than an uploadable image, so it always goes through the host copy.
		const bool intoStore = (_uploadFormat != PixelFormat::R8 && _uploadFormat != PixelFormat::RG8 &&
			!_isPaletteTexture && _pixels == nullptr && EnsureStore());
		std::uint8_t* base = (_uploadFormat == PixelFormat::R8 || intoStore ? _store : _pixels);
		const std::int32_t dstStride = (intoStore ? _storeStride : _strideBytes);
		if (base == nullptr) {
			return;
		}
		if (_uploadFormat == PixelFormat::R8 || intoStore) {
			// The patch goes straight into the RDP store, which the in-flight frame may still DMA
			WaitIfInFlight(_lastSampledFrame);
		}

		for (std::int32_t y = 0; y < height; y++) {
			const std::int32_t dstY = yoffset + y;
			if (dstY < 0 || dstY >= _height) {
				continue;
			}
			std::int32_t dstX = xoffset;
			std::int32_t copyW = width;
			std::int32_t srcX0 = 0;
			if (dstX < 0) {
				srcX0 = -dstX;
				copyW += dstX;
				dstX = 0;
			}
			if (dstX + copyW > _width) {
				copyW = _width - dstX;
			}
			if (copyW <= 0) {
				continue;
			}
			const std::uint8_t* srcRow = static_cast<const std::uint8_t*>(data) + std::size_t(y) * std::size_t(width) * srcBpp + std::size_t(srcX0) * srcBpp;
			if (intoStore) {
				std::uint16_t* dstRow = reinterpret_cast<std::uint16_t*>(base + std::size_t(dstY) * dstStride) + dstX;
				if (srcBpp == dstBpp) {
					for (std::int32_t x = 0; x < copyW; x++) {
						dstRow[x] = PackStoreTexel(srcRow + x * srcBpp);
					}
				} else {
					// Same widening as the byte path below (leading channels copied, the rest opaque),
					// through one texel of scratch so the packing sees the uploaded format's layout
					for (std::int32_t x = 0; x < copyW; x++) {
						alignas(4) std::uint8_t texel[4] = { 255, 255, 255, 255 };
						for (std::int32_t c = 0; c < copyBpp; c++) {
							texel[c] = srcRow[x * srcBpp + c];
						}
						dstRow[x] = PackStoreTexel(texel);
					}
				}
				continue;
			}
			std::uint8_t* dstRow = base + std::size_t(dstY) * dstStride + std::size_t(dstX) * dstBpp;
			if (srcBpp == dstBpp) {
				std::memcpy(dstRow, srcRow, std::size_t(copyW) * dstBpp);
			} else {
				for (std::int32_t x = 0; x < copyW; x++) {
					std::int32_t c = 0;
					for (; c < copyBpp; c++) {
						dstRow[x * dstBpp + c] = srcRow[x * srcBpp + c];
					}
					for (; c < dstBpp; c++) {
						dstRow[x * dstBpp + c] = 255;
					}
				}
			}
		}
		_contentVersion = ++_nextContentVersion;

		if (_isPaletteTexture) {
			RdpDevice::NotifyPaletteTextureChanged(this, yoffset, height);
		} else if (_uploadFormat == PixelFormat::R8 || intoStore) {
			// The patch went straight into the RDP store; only the cache writeback is left, over exactly
			// the rows that were written. Flushing the whole store instead is what a full-atlas upload
			// costs anyway, but a partial patch is not rare enough to pay it: the cinematics rewrite a
			// frame's worth of rows every frame, and a 510x512 store is half a megabyte of cache lines.
			const std::int32_t firstRow = (yoffset < 0 ? 0 : yoffset);
			const std::int32_t lastRow = (yoffset + height > _height ? _height : yoffset + height);
			if (lastRow > firstRow) {
				data_cache_hit_writeback(_store + std::size_t(firstRow) * _storeStride,
					std::size_t(lastRow - firstRow) * _storeStride);
			}
			_storeValid = true;
		} else {
			// Direct color reconverts (and RG8 rebakes) lazily at the next draw that samples it
			_storeValid = false;
		}
	}

	void RdpTexture::TexStorage2D(std::int32_t levels, PixelFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(levels);
		Allocate(format, width, height);
		if (_isRenderTarget) {
			SetRenderTarget(true);
		}
	}

	void RdpTexture::CompressedTexImage2D(std::int32_t level, PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t imageSize, const void* data)
	{
		static_cast<void>(level); static_cast<void>(format); static_cast<void>(width);
		static_cast<void>(height); static_cast<void>(imageSize); static_cast<void>(data);
	}

	void RdpTexture::CompressedTexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, std::int32_t imageSize, const void* data)
	{
		static_cast<void>(level); static_cast<void>(xoffset); static_cast<void>(yoffset); static_cast<void>(width);
		static_cast<void>(height); static_cast<void>(format); static_cast<void>(imageSize); static_cast<void>(data);
	}

	void RdpTexture::GetTexImage(std::int32_t level, PixelFormat format, bool bgr, void* pixels)
	{
		static_cast<void>(level);
		static_cast<void>(format);
		static_cast<void>(bgr);
		if (pixels == nullptr) {
			return;
		}
		if (_uploadFormat == PixelFormat::R8 && _store != nullptr) {
			// The store rows are padded; the caller's buffer is tight
			for (std::int32_t y = 0; y < _height; y++) {
				std::memcpy(static_cast<std::uint8_t*>(pixels) + std::size_t(y) * _width,
					_store + std::size_t(y) * _storeStride, std::size_t(_width));
			}
		} else if (_pixels != nullptr) {
			std::memcpy(pixels, _pixels, _pixelsSize);
		}
	}

	void RdpTexture::SetMinFiltering(nCine::SamplerFilter filter)
	{
		_minFilter = filter;
	}

	void RdpTexture::SetMagFiltering(nCine::SamplerFilter filter)
	{
		_magFilter = filter;
	}

	void RdpTexture::SetWrap(SamplerWrapping wrap)
	{
		_wrap = wrap;
	}

	void RdpTexture::SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a)
	{
		_swizzle[0] = r;
		_swizzle[1] = g;
		_swizzle[2] = b;
		_swizzle[3] = a;
	}

	void RdpTexture::SetMaxLevel(std::int32_t maxLevel)
	{
		static_cast<void>(maxLevel);
	}

	void RdpTexture::SetUnpackAlignment(std::int32_t alignment)
	{
		static_cast<void>(alignment);
	}

	void RdpTexture::SetObjectLabel(StringView label)
	{
		// The shared palette texture is uploaded by ContentResolver under this exact name; its rows are
		// converted into TLUTs by the device instead of ever being sampled as a texture
		if (label == "Palettes"_s) {
			_isPaletteTexture = true;
			if (_store != nullptr) {
				WaitIfInFlight(_lastSampledFrame);
				free(_store);
				_store = nullptr;
				_storeSize = 0;
				_storeValid = false;
			}
			RdpDevice::RegisterPaletteTexture(this);
		}
	}
}
