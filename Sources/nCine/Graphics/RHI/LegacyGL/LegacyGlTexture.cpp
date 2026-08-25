#include "LegacyGlTexture.h"
#include "LegacyGlApi.h"
#include "LegacyGlDevice.h"
#include "../../../../Main.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <malloc.h>


using namespace Death::Containers::Literals;

namespace nCine::RHI::LegacyGL
{
	namespace
	{
		// The size a page of the given extent is actually stored at. Where the GL samples non-power-of-two
		// textures the page IS its extent; where it does not, it is rounded up and the padding replicates
		// the edge texel (see BuildPage) so a bilinear tap at the last real texel does not fetch black.
		std::int32_t StoredExtent(std::int32_t value)
		{
			if (LegacyGlDevice::SupportsNonPowerOfTwo()) {
				return value;
			}
			std::int32_t result = 1;
			while (result < value && result < LegacyGlTexture::MaxPageDimension) {
				result <<= 1;
			}
			return result;
		}
	}

	std::uint32_t LegacyGlTexture::_nextHandle = 0;
	std::uint32_t LegacyGlTexture::_nextContentVersion = 0;

	LegacyGlTexture::LegacyGlTexture(TextureTarget target)
		: _handle(++_nextHandle), _contentVersion(0), _target(target), _format(PixelFormat::Unknown),
			_uploadFormat(PixelFormat::Unknown), _width(0), _height(0), _strideBytes(0), _bytesPerPixel(0),
			_minFilter(nCine::SamplerFilter::Nearest), _magFilter(nCine::SamplerFilter::Nearest),
			_wrap(SamplerWrapping::ClampToEdge), _textureUnit(0), _isRenderTarget(false), _isPaletteTexture(false),
			_pageStore(nullptr), _pageStoreSize(0), _pageBytesPerTexel(0), _pageDimension(MaxPageDimension), _pagesX(0), _pagesY(0),
			_pageStoreValid(false), _bakedStores{}, _activeBakedStore(-1), _nextBakedStore(0)
	{
		_swizzle[0] = SwizzleChannel::Red;
		_swizzle[1] = SwizzleChannel::Green;
		_swizzle[2] = SwizzleChannel::Blue;
		_swizzle[3] = SwizzleChannel::Alpha;
	}

	LegacyGlTexture::~LegacyGlTexture()
	{
		// Clear from every unit so a destroyed texture can't dangle in the device's bind tracking (which
		// also flushes anything batched that still refers to its texture objects)
		LegacyGlDevice::UnbindTexture(this);
		FreePageStores();
	}

	std::int32_t LegacyGlTexture::BytesPerPixel(PixelFormat format)
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

	void LegacyGlTexture::Allocate(PixelFormat format, std::int32_t width, std::int32_t height)
	{
		_uploadFormat = format;
		_format = format;
		_width = width;
		_height = height;
		_bytesPerPixel = BytesPerPixel(_format);
		_strideBytes = width * _bytesPerPixel;
		_pixels.assign(std::size_t(_strideBytes) * std::size_t(height > 0 ? height : 0), std::uint8_t(0));
		FreePageStores();
		PlanPages();
		_contentVersion = ++_nextContentVersion;
	}

	void LegacyGlTexture::PlanPages()
	{
		_pages.clear();
		_pagesX = 0;
		_pagesY = 0;
		_pageBytesPerTexel = 0;
		if (_width <= 0 || _height <= 0) {
			return;
		}

		switch (_uploadFormat) {
			case PixelFormat::R8:
			case PixelFormat::RG8:
			case PixelFormat::RGB565:
			case PixelFormat::RGB8:
			case PixelFormat::RGBA8:
				// Everything is uploaded as RGBA8. A legacy GL has no palette format to resolve indices
				// with and nothing to gain from a 16-bit store (the driver would expand it anyway on
				// anything this backend runs on), so one store format keeps the upload path single.
				_pageBytesPerTexel = 4;
				break;
			default:
				return;
		}

		// The device's reported limit is the real bound, and it is only known once a context exists, so
		// the split is decided here and remembered: AcquirePage() has to divide by the same number this
		// store was built with, even if it is asked after a context change. It is also the number the
		// tileset chunking sizes its atlases to, so content built for this device is never paged.
		_pageDimension = LegacyGlDevice::GetMaxTextureDimension();
		_pagesX = (_width + _pageDimension - 1) / _pageDimension;
		_pagesY = (_height + _pageDimension - 1) / _pageDimension;
		_pages.reserve(std::size_t(_pagesX) * std::size_t(_pagesY));
		for (std::int32_t py = 0; py < _pagesY; py++) {
			for (std::int32_t px = 0; px < _pagesX; px++) {
				Page page;
				page.OriginX = px * _pageDimension;
				page.OriginY = py * _pageDimension;
				page.Width = std::min(_pageDimension, _width - page.OriginX);
				page.Height = std::min(_pageDimension, _height - page.OriginY);
				// The draw path scales texel coordinates by the STORED size, so any padding is never
				// sampled - except by a bilinear tap at the last real texel, which is why BuildPage
				// replicates into it
				page.PaddedWidth = StoredExtent(page.Width);
				page.PaddedHeight = StoredExtent(page.Height);
				_pages.push_back(page);
			}
		}
		if (_pagesX > 1 || _pagesY > 1) {
			LOGI("Texture {}x{} split into {}x{} pages", _width, _height, _pagesX, _pagesY);
		}
	}

	void LegacyGlTexture::InvalidatePageStore()
	{
		_pageStoreValid = false;
		_activeBakedStore = -1;
		for (std::int32_t i = 0; i < BakedStoreCount; i++) {
			_bakedStores[i].Valid = false;
		}
	}

	void LegacyGlTexture::FreePageStores()
	{
		// The texture objects go with the stores that own them: the plain one keeps its pages' textures in
		// the page list, each bake slot keeps its own set
		for (Page& page : _pages) {
			if (page.GlTexture != 0) {
				GLuint name = page.GlTexture;
				glDeleteTextures(1, &name);
				page.GlTexture = 0;
				page.Uploaded = false;
			}
		}
		if (_pageStore != nullptr) {
			std::free(_pageStore);
			_pageStore = nullptr;
			_pageStoreSize = 0;
		}
		for (std::int32_t i = 0; i < BakedStoreCount; i++) {
			for (std::uint32_t& name : _bakedStores[i].PageTextures) {
				if (name != 0) {
					GLuint texture = name;
					glDeleteTextures(1, &texture);
					name = 0;
				}
			}
			_bakedStores[i].PageTextures.clear();
			if (_bakedStores[i].Data != nullptr) {
				std::free(_bakedStores[i].Data);
				_bakedStores[i].Data = nullptr;
			}
			_bakedStores[i].Valid = false;
		}
		_activeBakedStore = -1;
		_nextBakedStore = 0;
		_pageStoreValid = false;
	}

	void LegacyGlTexture::BuildPage(const Page& page, std::uint8_t* dst)
	{
		const std::int32_t rowTexels = page.PaddedWidth;
		const std::uint8_t* const hostBase = _pixels.data() + std::size_t(page.OriginY) * _strideBytes
			+ std::size_t(page.OriginX) * _bytesPerPixel;

		for (std::int32_t y = 0; y < page.Height; y++) {
			const std::uint8_t* DEATH_RESTRICT src = hostBase + std::size_t(y) * _strideBytes;
			std::uint8_t* DEATH_RESTRICT row = dst + std::size_t(y) * std::size_t(rowTexels) * 4;
			switch (_uploadFormat) {
				case PixelFormat::RGBA8:
					std::memcpy(row, src, std::size_t(page.Width) * 4);
					break;
				case PixelFormat::RGB8:
					for (std::int32_t x = 0; x < page.Width; x++) {
						row[x * 4 + 0] = src[0];
						row[x * 4 + 1] = src[1];
						row[x * 4 + 2] = src[2];
						row[x * 4 + 3] = 255;
						src += 3;
					}
					break;
				case PixelFormat::RGB565: {
					const std::uint16_t* DEATH_RESTRICT in = reinterpret_cast<const std::uint16_t*>(src);
					for (std::int32_t x = 0; x < page.Width; x++) {
						const std::uint32_t texel = in[x];
						const std::uint32_t r5 = (texel >> 11) & 0x1F, g6 = (texel >> 5) & 0x3F, b5 = texel & 0x1F;
						// Bit replication rather than a shift, so white stays white
						row[x * 4 + 0] = std::uint8_t((r5 << 3) | (r5 >> 2));
						row[x * 4 + 1] = std::uint8_t((g6 << 2) | (g6 >> 4));
						row[x * 4 + 2] = std::uint8_t((b5 << 3) | (b5 >> 2));
						row[x * 4 + 3] = 255;
					}
					break;
				}
				default:
					// R8/RG8 are indexed and never reach here - they go through BuildBakedPage, which is
					// what NeedsPaletteBake() tells the draw path
					for (std::int32_t x = 0; x < page.Width; x++) {
						row[x * 4 + 0] = row[x * 4 + 1] = row[x * 4 + 2] = src[x * _bytesPerPixel];
						row[x * 4 + 3] = 255;
					}
					break;
			}
			// The padding columns are only ever reached by a bilinear tap at the last real texel, so they
			// repeat it rather than being left undefined
			for (std::int32_t x = page.Width; x < rowTexels; x++) {
				std::memcpy(row + std::size_t(x) * 4, row + std::size_t(page.Width - 1) * 4, 4);
			}
		}
		for (std::int32_t y = page.Height; y < page.PaddedHeight; y++) {
			std::memcpy(dst + std::size_t(y) * std::size_t(rowTexels) * 4,
				dst + std::size_t(page.Height - 1) * std::size_t(rowTexels) * 4, std::size_t(rowTexels) * 4);
		}
	}

	void LegacyGlTexture::BuildBakedPage(const Page& page, std::uint8_t* dst, const std::uint32_t* paletteRgba)
	{
		const std::int32_t rowTexels = page.PaddedWidth;
		const std::uint8_t* const hostBase = _pixels.data() + std::size_t(page.OriginY) * _strideBytes
			+ std::size_t(page.OriginX) * _bytesPerPixel;
		const bool hasSeparateAlpha = (_uploadFormat == PixelFormat::RG8);

		for (std::int32_t y = 0; y < page.Height; y++) {
			const std::uint8_t* DEATH_RESTRICT src = hostBase + std::size_t(y) * _strideBytes;
			std::uint8_t* DEATH_RESTRICT row = dst + std::size_t(y) * std::size_t(rowTexels) * 4;
			for (std::int32_t x = 0; x < page.Width; x++) {
				const std::uint32_t entry = paletteRgba[src[0]];
				// A palette entry is an RGBA8 value with red in the lowest byte
				row[x * 4 + 0] = std::uint8_t(entry & 0xFF);
				row[x * 4 + 1] = std::uint8_t((entry >> 8) & 0xFF);
				row[x * 4 + 2] = std::uint8_t((entry >> 16) & 0xFF);
				// Index+alpha content carries its own coverage; a plain indexed store takes the entry's
				const std::uint32_t alpha = (hasSeparateAlpha ? src[1] : ((entry >> 24) & 0xFF));
				row[x * 4 + 3] = std::uint8_t(alpha);
				src += _bytesPerPixel;
			}
			for (std::int32_t x = page.Width; x < rowTexels; x++) {
				std::memcpy(row + std::size_t(x) * 4, row + std::size_t(page.Width - 1) * 4, 4);
			}
		}
		for (std::int32_t y = page.Height; y < page.PaddedHeight; y++) {
			std::memcpy(dst + std::size_t(y) * std::size_t(rowTexels) * 4,
				dst + std::size_t(page.Height - 1) * std::size_t(rowTexels) * 4, std::size_t(rowTexels) * 4);
		}
	}

	bool LegacyGlTexture::UploadPage(Page& page)
	{
		if (page.Data == nullptr) {
			return false;
		}
		if (page.GlTexture == 0) {
			GLuint name = 0;
			glGenTextures(1, &name);
			if (name == 0) {
				return false;
			}
			page.GlTexture = name;
			page.Uploaded = false;
		}
		glBindTexture(GL_TEXTURE_2D, page.GlTexture);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		// The store is exactly the texture: padded to a power of two, RGBA8, one page per object
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, page.PaddedWidth, page.PaddedHeight, 0,
			GL_RGBA, GL_UNSIGNED_BYTE, page.Data);
		// Filters and wrapping are per-draw state (the device sets them from the material), but a texture
		// with no mip chain must not be left on a mipmapping filter or it samples as incomplete
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		page.Uploaded = true;
		// The device caches the bound texture and knows nothing about this binding
		LegacyGlDevice::InvalidateStateCache();
		return true;
	}

	bool LegacyGlTexture::RefreshPageStore()
	{
		if (_pages.empty() || _pageBytesPerTexel <= 0 || _pixels.empty() || _isPaletteTexture) {
			return false;
		}
		// An indexed store has no colours of its own - the bake owns it (see EnsureBakedStore)
		if (IsIndexed()) {
			return false;
		}

		std::size_t size = 0;
		for (const Page& page : _pages) {
			size += std::size_t(page.PaddedWidth) * std::size_t(page.PaddedHeight) * 4;
		}
		if (_pageStore == nullptr || _pageStoreSize != size) {
			if (_pageStore != nullptr) {
				std::free(_pageStore);
			}
			_pageStore = static_cast<std::uint8_t*>(std::malloc(size));
			_pageStoreSize = (_pageStore != nullptr ? size : 0);
			if (_pageStore == nullptr) {
				LOGE("Out of memory allocating a {} B texture store for a {}x{} texture", size, _width, _height);
				return false;
			}
		}

		std::size_t offset = 0;
		for (Page& page : _pages) {
			page.Data = _pageStore + offset;
			page.Uploaded = false;
			BuildPage(page, _pageStore + offset);
			if (!UploadPage(page)) {
				return false;
			}
			offset += std::size_t(page.PaddedWidth) * std::size_t(page.PaddedHeight) * 4;
		}
		_pageStoreValid = true;
		return true;
	}

	bool LegacyGlTexture::EnsureBakedStore(const std::uint32_t* paletteRow, std::uint32_t paletteRowIndex,
		std::uint32_t paletteGeneration, const void* palette)
	{
		if (!IsIndexed() || paletteRow == nullptr || _pixels.empty() || _pages.empty()) {
			return false;
		}

		// A bake that is already the active store needs nothing at all, which is the case for every draw of
		// the frame once the sprite's palette row has been seen once
		for (std::int32_t i = 0; i < BakedStoreCount; i++) {
			const BakedStore& store = _bakedStores[i];
			if (store.Valid && store.PaletteRow == paletteRowIndex && store.Palette == palette &&
				store.PaletteGeneration == paletteGeneration && store.ContentVersion == _contentVersion) {
				if (_activeBakedStore != i) {
					// Switching back to a bake that is still resident: its pages keep their own texture
					// objects, so nothing is converted or uploaded again
					_activeBakedStore = i;
					std::size_t offset = 0;
					for (Page& page : _pages) {
						page.Data = store.Data + offset;
						page.GlTexture = store.PageTextures[std::size_t(&page - _pages.data())];
						offset += std::size_t(page.PaddedWidth) * std::size_t(page.PaddedHeight) * 4;
					}
				}
				_pageStoreValid = true;
				return true;
			}
		}

		std::size_t size = 0;
		for (const Page& page : _pages) {
			size += std::size_t(page.PaddedWidth) * std::size_t(page.PaddedHeight) * 4;
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
				LOGW("More than {} palette rows used with one indexed+alpha texture, expect rebuild churn", BakedStoreCount);
			}
		}

		BakedStore& store = _bakedStores[slot];
		if (store.Data == nullptr) {
			store.Data = static_cast<std::uint8_t*>(std::malloc(size));
			if (store.Data == nullptr) {
				LOGE("Out of memory allocating a {} B baked store for a {}x{} texture", size, _width, _height);
				return false;
			}
		}

		std::size_t offset = 0;
		store.PageTextures.resize(_pages.size(), 0);
		for (std::size_t i = 0; i < _pages.size(); i++) {
			Page& page = _pages[i];
			page.Data = store.Data + offset;
			// Each bake slot keeps its own texture objects, so switching palette rows back and forth
			// costs neither a conversion nor an upload
			page.GlTexture = store.PageTextures[i];
			page.Uploaded = false;
			BuildBakedPage(page, store.Data + offset, paletteRow);
			if (!UploadPage(page)) {
				return false;
			}
			store.PageTextures[i] = page.GlTexture;
			offset += std::size_t(page.PaddedWidth) * std::size_t(page.PaddedHeight) * 4;
		}

		store.Valid = true;
		store.PaletteRow = paletteRowIndex;
		store.PaletteGeneration = paletteGeneration;
		store.ContentVersion = _contentVersion;
		store.Palette = palette;
		_activeBakedStore = slot;
		_pageStoreValid = true;
		return true;
	}

	const LegacyGlTexture::Page* LegacyGlTexture::AcquirePage(std::int32_t texelX, std::int32_t texelY)
	{
		if (_pages.empty()) {
			return nullptr;
		}
		if (_isRenderTarget) {
			// GL rendered into the texture object itself; there is nothing to convert or upload
			return (_pages[0].GlTexture != 0 ? &_pages[0] : nullptr);
		}
		if (!_pageStoreValid && !RefreshPageStore()) {
			return nullptr;
		}

		const std::int32_t px = (texelX < 0 ? 0 : (texelX >= _width ? _width - 1 : texelX)) / _pageDimension;
		const std::int32_t py = (texelY < 0 ? 0 : (texelY >= _height ? _height - 1 : texelY)) / _pageDimension;
		return &_pages[std::size_t(py) * std::size_t(_pagesX) + std::size_t(px)];
	}

	void* LegacyGlTexture::MapStreamingTexels(std::int32_t& strideBytes)
	{
		// Declined on this backend. The fast path exists so a video frame can be written straight into the
		// store in the format the hardware samples; here that store is RGBA8 (see PlanPages), which is
		// not what the cinematic decoder produces, so letting it fall back to an ordinary upload converts
		// once instead of converting AND copying.
		static_cast<void>(strideBytes);
		return nullptr;
	}

	void LegacyGlTexture::SetRenderTarget(bool isRenderTarget)
	{
		if (_isRenderTarget == isRenderTarget) {
			return;
		}
		// A render target's texture object is one of the pages, so dropping the stores drops it too
		_isRenderTarget = isRenderTarget;
		FreePageStores();
		PlanPages();
		if (!isRenderTarget || _width <= 0 || _height <= 0) {
			return;
		}
		if (_pages.size() != 1) {
			LOGE("A {}x{} render target cannot be one texture (the page limit is {})", _width, _height,
				MaxPageDimension);
			return;
		}

		// A render target is a texture object GL renders into, so it is created at its REAL size rather
		// than padded: a framebuffer attachment is sampled with the same coordinates it was drawn with,
		// and every GL that can attach a texture can attach a non-power-of-two one.
		Page& page = _pages[0];
		page.PaddedWidth = _width;
		page.PaddedHeight = _height;
		GLuint name = 0;
		glGenTextures(1, &name);
		if (name == 0) {
			LOGE("Cannot create a {}x{} render-target texture", _width, _height);
			return;
		}
		page.GlTexture = name;
		glBindTexture(GL_TEXTURE_2D, page.GlTexture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _width, _height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		LegacyGlDevice::InvalidateStateCache();

		// A render target has no host content worth keeping - the rasterizer owns every texel
		_pixels = {};
		page.Data = nullptr;
		page.Uploaded = true;
		_pageStoreValid = true;
	}

	bool LegacyGlTexture::Bind(std::uint32_t textureUnit) const
	{
		_textureUnit = textureUnit;
		LegacyGlDevice::BindTexture(textureUnit, this);
		return true;
	}

	bool LegacyGlTexture::Unbind() const
	{
		LegacyGlDevice::BindTexture(_textureUnit, nullptr);
		return true;
	}

	bool LegacyGlTexture::Unbind(std::uint32_t textureUnit)
	{
		LegacyGlDevice::BindTexture(textureUnit, nullptr);
		return true;
	}

	void LegacyGlTexture::TexImage2D(std::int32_t level, PixelFormat format, bool bgr, std::int32_t width, std::int32_t height, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0) {
			return;		// Level 0 only
		}
		Allocate(format, width, height);
		if (data != nullptr && !_pixels.empty()) {
			std::memcpy(_pixels.data(), data, _pixels.size());
			_contentVersion = ++_nextContentVersion;
		}
		if (_isPaletteTexture) {
			LegacyGlDevice::NotifyPaletteTextureChanged(this, 0, _height);
		} else {
			InvalidatePageStore();
		}
	}

	void LegacyGlTexture::TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0 || data == nullptr || _pixels.empty()) {
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
			LegacyGlDevice::NotifyPaletteTextureChanged(this, yoffset, height);
		} else {
			// v1: any sub-update rebuilds the whole page store on the next draw (sub-updates are rare - tileset
			// overrides at load time, and the palette texture, which is intercepted above)
			InvalidatePageStore();
		}
	}

	void LegacyGlTexture::TexStorage2D(std::int32_t levels, PixelFormat format, std::int32_t width, std::int32_t height)
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

	void LegacyGlTexture::CompressedTexImage2D(std::int32_t level, PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t imageSize, const void* data)
	{
		static_cast<void>(level); static_cast<void>(format); static_cast<void>(width);
		static_cast<void>(height); static_cast<void>(imageSize); static_cast<void>(data);
	}

	void LegacyGlTexture::CompressedTexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, std::int32_t imageSize, const void* data)
	{
		static_cast<void>(level); static_cast<void>(xoffset); static_cast<void>(yoffset); static_cast<void>(width);
		static_cast<void>(height); static_cast<void>(format); static_cast<void>(imageSize); static_cast<void>(data);
	}

	void LegacyGlTexture::GetTexImage(std::int32_t level, PixelFormat format, bool bgr, void* pixels)
	{
		static_cast<void>(level);
		static_cast<void>(format);
		static_cast<void>(bgr);
		if (pixels != nullptr && !_pixels.empty()) {
			std::memcpy(pixels, _pixels.data(), _pixels.size());
		}
	}

	void LegacyGlTexture::SetMinFiltering(nCine::SamplerFilter filter)
	{
		_minFilter = filter;
	}

	void LegacyGlTexture::SetMagFiltering(nCine::SamplerFilter filter)
	{
		_magFilter = filter;
	}

	void LegacyGlTexture::SetWrap(SamplerWrapping wrap)
	{
		_wrap = wrap;
	}

	void LegacyGlTexture::SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a)
	{
		_swizzle[0] = r;
		_swizzle[1] = g;
		_swizzle[2] = b;
		_swizzle[3] = a;
	}

	void LegacyGlTexture::SetMaxLevel(std::int32_t maxLevel)
	{
		static_cast<void>(maxLevel);
	}

	void LegacyGlTexture::SetUnpackAlignment(std::int32_t alignment)
	{
		static_cast<void>(alignment);
	}

	void LegacyGlTexture::SetObjectLabel(StringView label)
	{
		// The shared palette texture is uploaded by ContentResolver under this exact name. It is never
		// sampled here - its rows are what indexed textures are baked through - so it keeps only the host
		// store and no texture object is ever built for it
		if (label == "Palettes"_s) {
			_isPaletteTexture = true;
			FreePageStores();
			LegacyGlDevice::RegisterPaletteTexture(this);
		}
	}
}
