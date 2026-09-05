#include "PicaTexture.h"
#include "PicaDevice.h"
#include "../../../../Main.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <3ds.h>
#include <citro3d.h>

using namespace Death::Containers::Literals;

namespace nCine::RHI::PICA
{
	namespace
	{
		// Rounds a texture dimension up to the next power of two the GPU can address, no smaller than one
		// 8x8 tile in either direction
		std::int32_t NextPow2(std::int32_t value)
		{
			std::int32_t result = PicaTexture::MinPageDimension;
			while (result < value && result < PicaTexture::MaxPageDimension) {
				result <<= 1;
			}
			return result;
		}

		// GPU_RGBA4 keeps red in the HIGH nibble and alpha in the low one
		inline std::uint16_t Rgba4FromRgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
		{
			return std::uint16_t(((r >> 4) << 12) | ((g >> 4) << 8) | ((b >> 4) << 4) | (a >> 4));
		}

		/**
			@brief Texel offset of (x, y) within an 8x8 tile, in the GPU's Morton (Z) order

			The three low bits of x and y are interleaved with x in the even positions: (x0, y0, x1, y1, x2, y2)
			from the least significant bit up, which is the order both the texture unit and the framebuffer use.
		*/
		constexpr std::uint8_t MortonOffset(std::int32_t x, std::int32_t y)
		{
			return std::uint8_t((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3));
		}

		struct MortonTable
		{
			std::uint8_t Offsets[64];
			constexpr MortonTable() : Offsets{} {
				for (std::int32_t y = 0; y < 8; y++) {
					for (std::int32_t x = 0; x < 8; x++) {
						Offsets[y * 8 + x] = MortonOffset(x, y);
					}
				}
			}
		};
		constexpr MortonTable Morton;

		// One band of eight linear rows at the widest page the backend builds - the scratch every page is
		// converted through, band by band, so the conversion never needs a linear copy of a whole page
		alignas(16) std::uint16_t bandScratch[8 * PicaTexture::MaxPageDimension];

		// The GPU addresses texture data in physical memory, 128-byte aligned rows of tiles being what the
		// texture unit and the transfer engine both assume
		constexpr std::size_t StoreAlignment = 128;
	}

	void PicaTexture::TileBand16(std::uint16_t* dst, const std::uint16_t* band, std::int32_t paddedWidth)
	{
		const std::int32_t tilesPerRow = paddedWidth / 8;
		for (std::int32_t tx = 0; tx < tilesPerRow; tx++) {
			std::uint16_t* DEATH_RESTRICT tile = dst + std::size_t(tx) * 64;
			const std::uint16_t* DEATH_RESTRICT src = band + std::size_t(tx) * 8;
			for (std::int32_t y = 0; y < 8; y++) {
				const std::uint16_t* DEATH_RESTRICT row = src + std::size_t(y) * paddedWidth;
				const std::uint8_t* DEATH_RESTRICT offsets = Morton.Offsets + y * 8;
				tile[offsets[0]] = row[0];
				tile[offsets[1]] = row[1];
				tile[offsets[2]] = row[2];
				tile[offsets[3]] = row[3];
				tile[offsets[4]] = row[4];
				tile[offsets[5]] = row[5];
				tile[offsets[6]] = row[6];
				tile[offsets[7]] = row[7];
			}
		}
	}

	std::uint32_t PicaTexture::_nextHandle = 0;
	std::uint32_t PicaTexture::_nextContentVersion = 0;

	PicaTexture::PicaTexture(TextureTarget target)
		: _handle(++_nextHandle), _contentVersion(0), _target(target), _format(PixelFormat::Unknown),
			_uploadFormat(PixelFormat::Unknown), _width(0), _height(0), _strideBytes(0), _bytesPerPixel(0),
			_minFilter(nCine::SamplerFilter::Nearest), _magFilter(nCine::SamplerFilter::Nearest),
			_wrap(SamplerWrapping::ClampToEdge), _textureUnit(0), _isRenderTarget(false), _isPaletteTexture(false),
			_gpuStore(nullptr), _gpuStoreSize(0), _picaFormat(GPU_RGBA4), _gpuBytesPerTexel(0), _pagesX(0), _pagesY(0),
			_gpuStoreValid(false), _bakedStores{}, _activeBakedStore(-1), _nextBakedStore(0), _hostCopyReleased(false),
			_renderTargetSurface(nullptr), _renderTargetSurfaceInVram(false), _renderTarget(nullptr), _renderTargetTex{}
	{
		_swizzle[0] = SwizzleChannel::Red;
		_swizzle[1] = SwizzleChannel::Green;
		_swizzle[2] = SwizzleChannel::Blue;
		_swizzle[3] = SwizzleChannel::Alpha;
	}

	PicaTexture::~PicaTexture()
	{
		// Clear from every unit so a destroyed texture can't dangle in the device's bind tracking, and make
		// sure the GPU is not still reading a store that is about to be freed
		PicaDevice::UnbindTexture(this);
		FreeGpuStores();
		FreeRenderTargetSurface();
	}

	std::int32_t PicaTexture::BytesPerPixel(PixelFormat format)
	{
		switch (format) {
			case PixelFormat::R8: return 1;
			case PixelFormat::RG8: return 2;
			case PixelFormat::RGB565: return 2;
			case PixelFormat::RGB8: return 3;
			case PixelFormat::RGBA8: return 4;
			default: return 0;
		}
	}

	void PicaTexture::Allocate(PixelFormat format, std::int32_t width, std::int32_t height)
	{
		_uploadFormat = format;
		_format = format;
		_width = width;
		_height = height;
		_bytesPerPixel = BytesPerPixel(_format);
		_strideBytes = width * _bytesPerPixel;
		_pixels.assign(std::size_t(_strideBytes) * std::size_t(height > 0 ? height : 0), std::uint8_t(0));
		FreeGpuStores();
		FreeRenderTargetSurface();
		PlanGpuStore();
		_contentVersion = ++_nextContentVersion;
	}

	void PicaTexture::PlanGpuStore()
	{
		_pages.clear();
		_pagesX = 0;
		_pagesY = 0;
		_gpuBytesPerTexel = 0;
		if (_width <= 0 || _height <= 0) {
			return;
		}

		switch (_uploadFormat) {
			case PixelFormat::R8:
			case PixelFormat::RG8:
			case PixelFormat::RGB8:
			case PixelFormat::RGBA8:
				// Both indexed formats are baked per palette row (there is no lookup table on this GPU) and true
				// colour converts to the same 16-bit format, halving the store and the sampling bandwidth over
				// RGBA8 for content whose colours are palette entries to begin with
				_picaFormat = GPU_RGBA4;
				_gpuBytesPerTexel = 2;
				break;
			case PixelFormat::RGB565:
				_picaFormat = GPU_RGB565;
				_gpuBytesPerTexel = 2;
				break;
			default:
				return;
		}
		if (_isRenderTarget) {
			// The GPU renders into a 16-bit colour buffer matching the display format
			_picaFormat = GPU_RGB565;
			_gpuBytesPerTexel = 2;
		}

		_pagesX = (_width + MaxPageDimension - 1) / MaxPageDimension;
		_pagesY = (_height + MaxPageDimension - 1) / MaxPageDimension;
		_pages.reserve(std::size_t(_pagesX) * std::size_t(_pagesY));
		for (std::int32_t py = 0; py < _pagesY; py++) {
			for (std::int32_t px = 0; px < _pagesX; px++) {
				Page page;
				page.OriginX = px * MaxPageDimension;
				page.OriginY = py * MaxPageDimension;
				page.Width = std::min(MaxPageDimension, _width - page.OriginX);
				page.Height = std::min(MaxPageDimension, _height - page.OriginY);
				page.PaddedWidth = NextPow2(page.Width);
				page.PaddedHeight = NextPow2(page.Height);
				_pages.push_back(page);
			}
		}
		if (_pagesX > 1 || _pagesY > 1) {
			LOGI("Texture {}x{} split into {}x{} GPU pages", _width, _height, _pagesX, _pagesY);
		}
	}

	void PicaTexture::ReleaseHostPixels()
	{
		// Emptying the vector is not enough: SmallVector keeps its allocation, and for a texture that
		// allocation is the entire point of releasing it
		_pixels.clear();
		_pixels.shrink(0);
	}

	void PicaTexture::ReleaseHostCopy()
	{
		// The GPU store is derived from these texels, so they only become redundant once it exists - and only
		// for content nothing writes again. An indexed texture rebuilds its bakes from the indices on every
		// palette change, streaming content is rewritten every frame, and the shared palette texture is read
		// by the bakes themselves, so none of those may give up the host copy.
		if (_pixels.empty() || _isRenderTarget || _isPaletteTexture || NeedsPaletteBake()) {
			return;
		}
		// Building the store here rather than at the first draw is what makes the texels redundant, and it
		// moves the work to load time where a hitch does not show
		if (!RefreshGpuStore()) {
			return;
		}

		ReleaseHostPixels();
		_hostCopyReleased = true;
	}

	void PicaTexture::InvalidateGpuStore()
	{
		_gpuStoreValid = false;
		_activeBakedStore = -1;
		for (std::int32_t i = 0; i < BakedStoreCount; i++) {
			_bakedStores[i].Valid = false;
		}
	}

	void PicaTexture::FreeGpuStores()
	{
		// Reached from the destructor and from Allocate() when a texture is re-initialized, both of which can
		// happen mid-frame while a command list that samples these stores is still to be run - so the memory
		// is handed to the device, which frees it once the GPU is provably done with the frame (see
		// PicaDevice::DeferredLinearFree)
		if (_gpuStore != nullptr) {
			PicaDevice::DeferredLinearFree(_gpuStore);
			_gpuStore = nullptr;
			_gpuStoreSize = 0;
		}
		for (std::int32_t i = 0; i < BakedStoreCount; i++) {
			if (_bakedStores[i].Data != nullptr) {
				PicaDevice::DeferredLinearFree(_bakedStores[i].Data);
				_bakedStores[i].Data = nullptr;
			}
			_bakedStores[i].Valid = false;
		}
		_activeBakedStore = -1;
		_nextBakedStore = 0;
		_gpuStoreValid = false;
	}

	void PicaTexture::FreeRenderTargetSurface()
	{
		if (_renderTarget != nullptr) {
			PicaDevice::UnbindRenderTargetSurface(_renderTarget);
			// The target is only a descriptor around the surface, so it can go right away; the surface itself
			// waits for the GPU like any other store
			C3D_RenderTargetDelete(_renderTarget);
			_renderTarget = nullptr;
		}
		if (_renderTargetSurface != nullptr) {
			PicaDevice::DeferredFree(_renderTargetSurface, _renderTargetSurfaceInVram);
			_renderTargetSurface = nullptr;
			_renderTargetSurfaceInVram = false;
		}
	}

	void PicaTexture::BuildPage(const Page& page, std::uint8_t* dst)
	{
		const std::uint8_t* const hostBase = _pixels.data() + std::size_t(page.OriginY) * _strideBytes
			+ std::size_t(page.OriginX) * _bytesPerPixel;
		std::uint16_t* DEATH_RESTRICT tiles = reinterpret_cast<std::uint16_t*>(dst);

		// Eight rows at a time: converted into the linear band scratch, then scattered into the band's tiles.
		// The GPU samples a texture BOTTOM-UP - texture coordinate v = 0 is the last row in memory and v = 1
		// the first (the convention tex3ds encodes for, and the one Citra's rasterizer flips t by) - so the
		// source's first row goes into the last memory row, and the padding rows come first in memory, where
		// v = 1 is never sampled. The padding columns and rows are only ever reached by a bilinear tap at the
		// very last texel, but leaving them uninitialised would show up as fringing there.
		for (std::int32_t bandY = 0; bandY < page.PaddedHeight; bandY += 8) {
			for (std::int32_t row = 0; row < 8; row++) {
				const std::int32_t y = page.PaddedHeight - 1 - (bandY + row);
				std::uint16_t* DEATH_RESTRICT out = bandScratch + std::size_t(row) * page.PaddedWidth;
				if (y >= page.Height) {
					std::memset(out, 0, std::size_t(page.PaddedWidth) * 2);
					continue;
				}
				const std::uint8_t* DEATH_RESTRICT src = hostBase + std::size_t(y) * _strideBytes;
				switch (_uploadFormat) {
					case PixelFormat::RGB565:
						// The GPU's RGB565 has red in the high bits, exactly like the engine's
						std::memcpy(out, src, std::size_t(page.Width) * 2);
						break;
					case PixelFormat::RGB8:
					case PixelFormat::RGBA8:
						if (_bytesPerPixel >= 4) {
							for (std::int32_t x = 0; x < page.Width; x++) {
								out[x] = Rgba4FromRgba(src[0], src[1], src[2], src[3]);
								src += 4;
							}
						} else {
							for (std::int32_t x = 0; x < page.Width; x++) {
								out[x] = Rgba4FromRgba(src[0], src[1], src[2], 255);
								src += _bytesPerPixel;
							}
						}
						break;
					default:
						std::memset(out, 0, std::size_t(page.Width) * 2);
						break;
				}
				std::memset(out + page.Width, 0, std::size_t(page.PaddedWidth - page.Width) * 2);
			}
			TileBand16(tiles + std::size_t(bandY) * page.PaddedWidth, bandScratch, page.PaddedWidth);
		}
	}

	void PicaTexture::BuildBakedPage(const Page& page, std::uint8_t* dst, const std::uint16_t* rgba4)
	{
		const std::uint8_t* const hostBase = _pixels.data() + std::size_t(page.OriginY) * _strideBytes
			+ std::size_t(page.OriginX) * _bytesPerPixel;
		std::uint16_t* DEATH_RESTRICT tiles = reinterpret_cast<std::uint16_t*>(dst);
		const bool hasAlphaByte = (_uploadFormat == PixelFormat::RG8);

		// Bottom-up like BuildPage(), see there
		for (std::int32_t bandY = 0; bandY < page.PaddedHeight; bandY += 8) {
			for (std::int32_t row = 0; row < 8; row++) {
				const std::int32_t y = page.PaddedHeight - 1 - (bandY + row);
				std::uint16_t* DEATH_RESTRICT out = bandScratch + std::size_t(row) * page.PaddedWidth;
				if (y >= page.Height) {
					std::memset(out, 0, std::size_t(page.PaddedWidth) * 2);
					continue;
				}
				const std::uint8_t* DEATH_RESTRICT src = hostBase + std::size_t(y) * _strideBytes;
				if (hasAlphaByte) {
					// Colour from the palette row the index points at, coverage from the texel's own alpha byte
					for (std::int32_t x = 0; x < page.Width; x++) {
						out[x] = std::uint16_t((rgba4[src[x * 2]] & 0xFFF0u) | (src[x * 2 + 1] >> 4));
					}
				} else {
					// Colour AND coverage from the palette entry
					for (std::int32_t x = 0; x < page.Width; x++) {
						out[x] = rgba4[src[x]];
					}
				}
				std::memset(out + page.Width, 0, std::size_t(page.PaddedWidth - page.Width) * 2);
			}
			TileBand16(tiles + std::size_t(bandY) * page.PaddedWidth, bandScratch, page.PaddedWidth);
		}
	}

	bool PicaTexture::RefreshGpuStore()
	{
		if (_pages.empty() || _gpuBytesPerTexel <= 0 || _pixels.empty() || _isPaletteTexture) {
			return false;
		}
		// An indexed texture has no store of its own - the bake owns it (see EnsureBakedStore)
		if (NeedsPaletteBake()) {
			return false;
		}

		std::size_t size = 0;
		for (const Page& page : _pages) {
			size += std::size_t(page.PaddedWidth) * std::size_t(page.PaddedHeight) * std::size_t(_gpuBytesPerTexel);
		}
		if (_gpuStore == nullptr || _gpuStoreSize != size) {
			if (_gpuStore != nullptr) {
				// Replacing a store the GPU may not have read yet - a first allocation needs no such care
				PicaDevice::DeferredLinearFree(_gpuStore);
			}
			_gpuStore = static_cast<std::uint8_t*>(linearMemAlign(size, StoreAlignment));
			_gpuStoreSize = (_gpuStore != nullptr ? size : 0);
			if (_gpuStore == nullptr) {
				LOGE("Out of linear memory allocating a {} B GPU store for a {}x{} texture ({} B free)", size, _width, _height, linearSpaceFree());
				return false;
			}
		}

		std::size_t offset = 0;
		for (Page& page : _pages) {
			page.Data = _gpuStore + offset;
			BuildPage(page, _gpuStore + offset);
			offset += std::size_t(page.PaddedWidth) * std::size_t(page.PaddedHeight) * std::size_t(_gpuBytesPerTexel);
		}

		// The GPU reads the linear heap without looking at the data cache
		GSPGPU_FlushDataCache(_gpuStore, _gpuStoreSize);
		_gpuStoreValid = true;
		return true;
	}

	bool PicaTexture::EnsureBakedStore(const PicaTexture* palette, std::int32_t paletteOffset, std::uint32_t paletteGeneration)
	{
		if (!NeedsPaletteBake() || palette == nullptr || _pixels.empty() || _pages.empty()) {
			return false;
		}
		const std::uint8_t* const paletteBase = palette->GetPixels();
		if (paletteBase == nullptr) {
			return false;
		}

		// The bake reads 256 consecutive RGBA8 entries from the offset, so the offset has to leave that many
		// inside the palette texture
		const std::int32_t maxOffset = palette->GetWidth() * palette->GetHeight() - 256;
		if (paletteOffset < 0 || paletteOffset > maxOffset) {
			static bool warnedOffset = false;
			if (!warnedOffset) {
				warnedOffset = true;
				LOGW("Palette offset {} is outside the {}x{} palette texture (at most {}), skipping the bake",
					paletteOffset, palette->GetWidth(), palette->GetHeight(), maxOffset);
			}
			return false;
		}
		const std::uint32_t* const paletteRow = reinterpret_cast<const std::uint32_t*>(paletteBase) + paletteOffset;
		const std::uint32_t paletteRowIndex = std::uint32_t(paletteOffset);

		// A bake that is already the active store needs nothing at all, which is the case for every draw of
		// the frame once the sprite's palette row has been seen once
		for (std::int32_t i = 0; i < BakedStoreCount; i++) {
			const BakedStore& store = _bakedStores[i];
			if (store.Valid && store.PaletteRow == paletteRowIndex && store.Palette == palette &&
				store.PaletteGeneration == paletteGeneration && store.ContentVersion == _contentVersion) {
				if (_activeBakedStore != i) {
					_activeBakedStore = i;
					std::size_t offset = 0;
					for (Page& page : _pages) {
						page.Data = store.Data + offset;
						offset += std::size_t(page.PaddedWidth) * std::size_t(page.PaddedHeight) * 2;
					}
				}
				_gpuStoreValid = true;
				return true;
			}
		}

		std::size_t size = 0;
		for (const Page& page : _pages) {
			size += std::size_t(page.PaddedWidth) * std::size_t(page.PaddedHeight) * 2;
		}

		// Reuse a stale entry of the same row, then any free one, then the round-robin victim
		std::int32_t slot = -1;
		for (std::int32_t i = 0; i < BakedStoreCount && slot < 0; i++) {
			if (_bakedStores[i].Valid && _bakedStores[i].PaletteRow == paletteRowIndex &&
				_bakedStores[i].Palette == palette) {
				slot = i;
			}
		}
		for (std::int32_t i = 0; i < BakedStoreCount && slot < 0; i++) {
			if (!_bakedStores[i].Valid) {
				slot = i;
			}
		}
		if (slot < 0) {
			slot = _nextBakedStore;
			_nextBakedStore = (_nextBakedStore + 1) % BakedStoreCount;
			static bool warnedSlots = false;
			if (!warnedSlots) {
				warnedSlots = true;
				LOGW("More than {} palette rows used with one indexed texture, expect rebuild churn", BakedStoreCount);
			}
		}

		BakedStore& store = _bakedStores[slot];
		if (store.Data != nullptr && store.Valid) {
			// The GPU may still be sampling the previous bake of this slot from a command list that has not
			// run yet, so the memory is not written over but replaced
			PicaDevice::DeferredLinearFree(store.Data);
			store.Data = nullptr;
		}
		if (store.Data == nullptr) {
			store.Data = static_cast<std::uint8_t*>(linearMemAlign(size, StoreAlignment));
			if (store.Data == nullptr) {
				LOGE("Out of linear memory allocating a {} B baked store for a {}x{} texture ({} B free)", size, _width, _height, linearSpaceFree());
				return false;
			}
		}

		// The 256 palette entries collapse into packed RGBA4 once per bake, so the texel loop is one lookup
		// (and an alpha merge for index+alpha content) instead of unpacking and requantizing every pixel. A
		// palette entry is an RGBA8 value with red in the lowest byte.
		std::uint16_t rgba4[256];
		for (std::int32_t i = 0; i < 256; i++) {
			const std::uint32_t color = paletteRow[i];
			rgba4[i] = Rgba4FromRgba(std::uint8_t(color), std::uint8_t(color >> 8), std::uint8_t(color >> 16), std::uint8_t(color >> 24));
		}

		std::size_t offset = 0;
		for (Page& page : _pages) {
			page.Data = store.Data + offset;
			BuildBakedPage(page, store.Data + offset, rgba4);
			offset += std::size_t(page.PaddedWidth) * std::size_t(page.PaddedHeight) * 2;
		}
		GSPGPU_FlushDataCache(store.Data, size);

		store.Valid = true;
		store.PaletteRow = paletteRowIndex;
		store.PaletteGeneration = paletteGeneration;
		store.ContentVersion = _contentVersion;
		store.Palette = palette;
		_activeBakedStore = slot;
		_gpuStoreValid = true;
		return true;
	}

	const PicaTexture::Page* PicaTexture::AcquirePage(std::int32_t texelX, std::int32_t texelY)
	{
		if (_pages.empty()) {
			return nullptr;
		}
		if (_isRenderTarget) {
			// The GPU wrote the surface itself; there is nothing to convert
			return (_renderTargetSurface != nullptr ? &_pages[0] : nullptr);
		}
		if (!_gpuStoreValid && !RefreshGpuStore()) {
			return nullptr;
		}

		const std::int32_t px = (texelX < 0 ? 0 : (texelX >= _width ? _width - 1 : texelX)) / MaxPageDimension;
		const std::int32_t py = (texelY < 0 ? 0 : (texelY >= _height ? _height - 1 : texelY)) / MaxPageDimension;
		return &_pages[std::size_t(py) * std::size_t(_pagesX) + std::size_t(px)];
	}

	void* PicaTexture::MapStreamingTexels(std::int32_t& strideBytes)
	{
		// A tiled store has no row pitch a linear writer could use (see the class documentation)
		strideBytes = 0;
		return nullptr;
	}

	void PicaTexture::SetRenderTarget(bool isRenderTarget)
	{
		if (_isRenderTarget == isRenderTarget) {
			return;
		}
		_isRenderTarget = isRenderTarget;
		FreeGpuStores();
		FreeRenderTargetSurface();
		PlanGpuStore();
		if (!isRenderTarget || _width <= 0 || _height <= 0) {
			return;
		}
		if (_pages.size() != 1) {
			LOGE("A {}x{} render target cannot be sampled as one GPU texture (limit is {})", _width, _height, MaxPageDimension);
			return;
		}

		// The GPU renders into a 16-bit colour buffer whose dimensions are the padded page's. VRAM is worth
		// using here - unlike for sprite atlases, the target is written by the rasterizer every frame, and
		// VRAM is where the GPU's own bandwidth is - but the framebuffers and the screen target are already
		// in its 6 MB, so the linear heap is an accepted fallback.
		Page& page = _pages[0];
		const std::size_t size = std::size_t(page.PaddedWidth) * std::size_t(page.PaddedHeight) * 2;
		_renderTargetSurface = vramMemAlign(size, StoreAlignment);
		_renderTargetSurfaceInVram = (_renderTargetSurface != nullptr);
		if (_renderTargetSurface == nullptr) {
			_renderTargetSurface = linearMemAlign(size, StoreAlignment);
			if (_renderTargetSurface == nullptr) {
				LOGE("Out of memory allocating a {} B render-target surface ({}x{})", size, _width, _height);
				return;
			}
			LOGW("Render target {}x{} did not fit video memory and renders into the linear heap", _width, _height);
		}
		// A render target has no host content worth keeping - the rasterizer owns every texel - and its linear
		// store would be another quarter of a megabyte of main memory for nothing
		ReleaseHostPixels();

		// citro3d wraps a colour buffer it did not allocate through the texture the buffer IS: the descriptor
		// carries the address, the format and the padded size, and the target created from it renders into it
		static_assert(sizeof(C3D_Tex) <= sizeof(_renderTargetTex), "C3D_Tex storage is too small");
		C3D_Tex* tex = reinterpret_cast<C3D_Tex*>(_renderTargetTex);
		std::memset(tex, 0, sizeof(C3D_Tex));
		tex->data = _renderTargetSurface;
		tex->fmt = _picaFormat;
		tex->size = std::uint32_t(size);
		tex->width = std::uint16_t(page.PaddedWidth);
		tex->height = std::uint16_t(page.PaddedHeight);
		tex->param = GPU_TEXTURE_MODE(GPU_TEX_2D);
		_renderTarget = C3D_RenderTargetCreateFromTex(tex, GPU_TEXFACE_2D, 0, -1);
		if (_renderTarget == nullptr) {
			LOGE("Cannot create a {}x{} render target", _width, _height);
			PicaDevice::DeferredFree(_renderTargetSurface, _renderTargetSurfaceInVram);
			_renderTargetSurface = nullptr;
			return;
		}

		page.Data = _renderTargetSurface;
		_gpuStoreValid = true;

		// A fresh target starts black (the memory fill runs on the GPU and is sequenced with the frame)
		PicaDevice::ClearRenderTargetSurface(_renderTarget);
	}

	bool PicaTexture::Bind(std::uint32_t textureUnit) const
	{
		_textureUnit = textureUnit;
		PicaDevice::BindTexture(textureUnit, this);
		return true;
	}

	bool PicaTexture::Unbind() const
	{
		PicaDevice::BindTexture(_textureUnit, nullptr);
		return true;
	}

	bool PicaTexture::Unbind(std::uint32_t textureUnit)
	{
		PicaDevice::BindTexture(textureUnit, nullptr);
		return true;
	}

	void PicaTexture::TexImage2D(std::int32_t level, PixelFormat format, bool bgr, std::int32_t width, std::int32_t height, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0) {
			return;		// Level 0 only
		}
		Allocate(format, width, height);
		if (data != nullptr && _pixels.empty() && _hostCopyReleased) {
			LOGW("A {}x{} texture is being uploaded after its host copy was released, so the upload is lost",
				width, height);
		}
		if (data != nullptr && !_pixels.empty()) {
			std::memcpy(_pixels.data(), data, _pixels.size());
			_contentVersion = ++_nextContentVersion;
		}
		if (_isPaletteTexture) {
			PicaDevice::NotifyPaletteTextureChanged(this, 0, _height);
		} else {
			InvalidateGpuStore();
		}
	}

	void PicaTexture::TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0 || data == nullptr || _pixels.empty()) {
			if (data != nullptr && _hostCopyReleased) {
				LOGW("A {}x{} region of a texture is being uploaded after its host copy was released, so the "
					"upload is lost", width, height);
			}
			return;
		}
		const std::int32_t srcBpp = BytesPerPixel(format);
		const std::int32_t dstBpp = _bytesPerPixel;
		const std::int32_t copyBpp = (srcBpp < dstBpp ? srcBpp : dstBpp);

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
			std::uint8_t* dstRow = _pixels.data() + std::size_t(dstY) * _strideBytes + std::size_t(dstX) * dstBpp;
			if (srcBpp == dstBpp) {
				std::memcpy(dstRow, srcRow, std::size_t(copyW) * dstBpp);
			} else {
				// A narrower source fills the channels it has and opaque-fills the rest, which is what the
				// RGB8-into-RGBA8 uploads the loaders produce need
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
			PicaDevice::NotifyPaletteTextureChanged(this, yoffset, height);
		} else {
			// Any sub-update rebuilds the whole GPU store on the next draw (sub-updates are rare - tileset
			// overrides at load time, the cinematic frames, and the palette texture, which is intercepted above)
			InvalidateGpuStore();
		}
	}

	void PicaTexture::TexStorage2D(std::int32_t levels, PixelFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(levels);
		const bool wasRenderTarget = _isRenderTarget;
		Allocate(format, width, height);
		if (wasRenderTarget) {
			// Allocate() dropped the surface with the old size; re-attach one for the new one
			_isRenderTarget = false;
			SetRenderTarget(true);
		}
	}

	void PicaTexture::CompressedTexImage2D(std::int32_t level, PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t imageSize, const void* data)
	{
		static_cast<void>(level); static_cast<void>(format); static_cast<void>(width);
		static_cast<void>(height); static_cast<void>(imageSize); static_cast<void>(data);
	}

	void PicaTexture::CompressedTexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, std::int32_t imageSize, const void* data)
	{
		static_cast<void>(level); static_cast<void>(xoffset); static_cast<void>(yoffset); static_cast<void>(width);
		static_cast<void>(height); static_cast<void>(format); static_cast<void>(imageSize); static_cast<void>(data);
	}

	void PicaTexture::GetTexImage(std::int32_t level, PixelFormat format, bool bgr, void* pixels)
	{
		static_cast<void>(level);
		static_cast<void>(format);
		static_cast<void>(bgr);
		if (pixels != nullptr && !_pixels.empty()) {
			std::memcpy(pixels, _pixels.data(), _pixels.size());
		}
	}

	void PicaTexture::SetMinFiltering(nCine::SamplerFilter filter)
	{
		_minFilter = filter;
	}

	void PicaTexture::SetMagFiltering(nCine::SamplerFilter filter)
	{
		_magFilter = filter;
	}

	void PicaTexture::SetWrap(SamplerWrapping wrap)
	{
		_wrap = wrap;
	}

	void PicaTexture::SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a)
	{
		_swizzle[0] = r;
		_swizzle[1] = g;
		_swizzle[2] = b;
		_swizzle[3] = a;
	}

	void PicaTexture::SetMaxLevel(std::int32_t maxLevel)
	{
		static_cast<void>(maxLevel);
	}

	void PicaTexture::SetUnpackAlignment(std::int32_t alignment)
	{
		static_cast<void>(alignment);
	}

	void PicaTexture::SetObjectLabel(StringView label)
	{
		// The shared palette texture is uploaded by ContentResolver under this exact name; its rows are what
		// the bakes resolve indices through instead of a sampled texture, so it keeps only the host store
		if (label == "Palettes"_s) {
			_isPaletteTexture = true;
			FreeGpuStores();
			PicaDevice::RegisterPaletteTexture(this);
		}
	}
}
