#include "GxTexture.h"
#include "GxDevice.h"

#include "../../../../Main.h"

#include <cstring>
#include <malloc.h>		// memalign (libogc main memory must be 32-byte aligned for GX/DMA)

#include <ogc/cache.h>	// DCFlushRange

using namespace Death::Containers::Literals;

namespace nCine::RHI::GX
{
	namespace
	{
		// Rounds a texture dimension up to the given tile edge (GX stores textures as tiles)
		inline std::int32_t AlignUp(std::int32_t value, std::int32_t alignment)
		{
			return (value + alignment - 1) & ~(alignment - 1);
		}

		// Converts a linear 1-byte-per-texel index image to the tiled GX_TF_CI8 layout (8x4 texel tiles,
		// 32 bytes each, tiles ordered row-major)
		void TileCi8(std::uint8_t* DEATH_RESTRICT dst, const std::uint8_t* DEATH_RESTRICT src,
			std::int32_t width, std::int32_t height, std::int32_t srcStride)
		{
			const std::int32_t tilesX = AlignUp(width, 8) / 8;
			const std::int32_t tilesY = AlignUp(height, 4) / 4;
			for (std::int32_t ty = 0; ty < tilesY; ty++) {
				for (std::int32_t tx = 0; tx < tilesX; tx++) {
					std::uint8_t* tile = dst + std::size_t(ty * tilesX + tx) * 32;
					for (std::int32_t row = 0; row < 4; row++) {
						const std::int32_t y = ty * 4 + row;
						for (std::int32_t col = 0; col < 8; col++) {
							const std::int32_t x = tx * 8 + col;
							tile[row * 8 + col] = (x < width && y < height ? src[std::size_t(y) * srcStride + x] : 0);
						}
					}
				}
			}
		}

		// Converts a linear RGBA image (rgbaBpp = 4, or 3 with implicit opaque alpha, or an already-expanded
		// row source) to the tiled GX_TF_RGBA8 layout: 4x4 texel tiles of 64 bytes, the first 32 bytes
		// holding the AR pairs and the second 32 bytes the GB pairs of the 16 texels
		void TileRgba8(std::uint8_t* DEATH_RESTRICT dst, const std::uint8_t* DEATH_RESTRICT src,
			std::int32_t width, std::int32_t height, std::int32_t srcStride, std::int32_t srcBpp)
		{
			const std::int32_t tilesX = AlignUp(width, 4) / 4;
			const std::int32_t tilesY = AlignUp(height, 4) / 4;
			for (std::int32_t ty = 0; ty < tilesY; ty++) {
				for (std::int32_t tx = 0; tx < tilesX; tx++) {
					std::uint8_t* tile = dst + std::size_t(ty * tilesX + tx) * 64;
					for (std::int32_t row = 0; row < 4; row++) {
						const std::int32_t y = ty * 4 + row;
						for (std::int32_t col = 0; col < 4; col++) {
							const std::int32_t x = tx * 4 + col;
							std::uint8_t r = 0, g = 0, b = 0, a = 0;
							if (x < width && y < height) {
								const std::uint8_t* px = src + std::size_t(y) * srcStride + std::size_t(x) * srcBpp;
								r = px[0];
								g = (srcBpp >= 2 ? px[1] : std::uint8_t(255));
								b = (srcBpp >= 3 ? px[2] : std::uint8_t(255));
								a = (srcBpp >= 4 ? px[3] : std::uint8_t(255));
							}
							const std::int32_t i = row * 4 + col;
							tile[i * 2 + 0] = a;
							tile[i * 2 + 1] = r;
							tile[32 + i * 2 + 0] = g;
							tile[32 + i * 2 + 1] = b;
						}
					}
				}
			}
		}

		inline std::size_t TiledSizeCi8(std::int32_t width, std::int32_t height)
		{
			return std::size_t(AlignUp(width, 8)) * std::size_t(AlignUp(height, 4));
		}

		inline std::size_t TiledSizeRgba8(std::int32_t width, std::int32_t height)
		{
			return std::size_t(AlignUp(width, 4)) * std::size_t(AlignUp(height, 4)) * 4;
		}
	}

	std::uint32_t GxTexture::_nextHandle = 1;
	std::uint32_t GxTexture::_nextContentVersion = 0;

	GxTexture::GxTexture(TextureTarget target)
		: _handle(_nextHandle++), _contentVersion(0), _target(target), _format(PixelFormat::Unknown), _uploadFormat(PixelFormat::Unknown),
			_width(0), _height(0), _strideBytes(0), _bytesPerPixel(0),
			_minFilter(nCine::SamplerFilter::Nearest), _magFilter(nCine::SamplerFilter::Nearest), _wrap(SamplerWrapping::ClampToEdge),
			_textureUnit(0), _isRenderTarget(false), _isPaletteTexture(false),
			_tiledStore(nullptr), _tiledStoreSize(0), _texObjValid(false),
			_bakedSlots{}, _nextBakedSlot(0)
	{
		_swizzle[0] = SwizzleChannel::Red;
		_swizzle[1] = SwizzleChannel::Green;
		_swizzle[2] = SwizzleChannel::Blue;
		_swizzle[3] = SwizzleChannel::Alpha;
	}

	GxTexture::~GxTexture()
	{
		// Clear from the device so a destroyed texture can't dangle in the bound-texture table
		GxDevice::UnbindTexture(this);
		FreeTiledStores();
	}

	void GxTexture::FreeTiledStores()
	{
		if (_tiledStore != nullptr) {
			free(_tiledStore);
			_tiledStore = nullptr;
			_tiledStoreSize = 0;
		}
		_texObjValid = false;
		for (std::int32_t i = 0; i < BakedSlotCount; i++) {
			if (_bakedSlots[i].Store != nullptr) {
				free(_bakedSlots[i].Store);
				_bakedSlots[i].Store = nullptr;
			}
			_bakedSlots[i].Valid = false;
		}
	}

	std::int32_t GxTexture::BytesPerPixel(PixelFormat format)
	{
		switch (format) {
			case PixelFormat::R8: return 1;
			case PixelFormat::RG8: return 2;
			case PixelFormat::RGB8: return 3;
			case PixelFormat::RGBA8: return 4;
			default: return 0;
		}
	}

	void GxTexture::Allocate(PixelFormat format, std::int32_t width, std::int32_t height)
	{
		_uploadFormat = format;
		_format = format;
		_width = width;
		_height = height;
		_bytesPerPixel = BytesPerPixel(_format);
		_strideBytes = width * _bytesPerPixel;
		_pixels.assign(std::size_t(_strideBytes) * std::size_t(height > 0 ? height : 0), std::uint8_t(0));
		FreeTiledStores();
		_contentVersion = ++_nextContentVersion;
	}

	void GxTexture::InitTexObj(GXTexObj& obj, void* store, std::uint8_t gxFormat, bool ci)
	{
		const std::uint8_t wrap = (_wrap == SamplerWrapping::Repeat ? GX_REPEAT
			: (_wrap == SamplerWrapping::MirroredRepeat ? GX_MIRROR : GX_CLAMP));
		if (ci) {
			// The TLUT name is patched per draw by the device (GX_InitTexObjTlut) before loading the object
			GX_InitTexObjCI(&obj, store, std::uint16_t(_width), std::uint16_t(_height), gxFormat, wrap, wrap, GX_FALSE, GX_TLUT0);
		} else {
			GX_InitTexObj(&obj, store, std::uint16_t(_width), std::uint16_t(_height), gxFormat, wrap, wrap, GX_FALSE);
		}
		const std::uint8_t filter = (_magFilter == nCine::SamplerFilter::Linear ? GX_LINEAR : GX_NEAR);
		GX_InitTexObjFilterMode(&obj, filter, filter);
	}

	void GxTexture::RefreshTiledStore()
	{
		_texObjValid = false;
		if (_pixels.empty() || _width <= 0 || _height <= 0 || _isPaletteTexture) {
			return;
		}

		std::size_t required = 0;
		if (_uploadFormat == PixelFormat::R8) {
			required = TiledSizeCi8(_width, _height);
		} else if (_uploadFormat == PixelFormat::RGB8 || _uploadFormat == PixelFormat::RGBA8) {
			required = TiledSizeRgba8(_width, _height);
		} else {
			// RG8 keeps only the linear store; the tiled copy is baked per palette row on demand
			return;
		}

		if (_tiledStore == nullptr || _tiledStoreSize != required) {
			if (_tiledStore != nullptr) {
				free(_tiledStore);
			}
			_tiledStore = static_cast<std::uint8_t*>(memalign(32, required));
			_tiledStoreSize = required;
			if (_tiledStore == nullptr) {
				LOGE("Out of memory allocating a {} B tiled texture store", required);
				return;
			}
		}

		if (_uploadFormat == PixelFormat::R8) {
			TileCi8(_tiledStore, _pixels.data(), _width, _height, _strideBytes);
			InitTexObj(_texObj, _tiledStore, GX_TF_CI8, true);
		} else {
			TileRgba8(_tiledStore, _pixels.data(), _width, _height, _strideBytes, _bytesPerPixel);
			InitTexObj(_texObj, _tiledStore, GX_TF_RGBA8, false);
		}
		DCFlushRange(_tiledStore, std::uint32_t(_tiledStoreSize));
		GX_InvalidateTexAll();
		_texObjValid = true;
	}

	GXTexObj* GxTexture::GetTexObj()
	{
		if (!_texObjValid && _isRenderTarget) {
			// A render target's tiled store is written by EFB copies; (re)create it lazily
			SetRenderTarget(true);
		}
		return (_texObjValid ? &_texObj : nullptr);
	}

	GXTexObj* GxTexture::EnsureBakedRgba(const std::uint32_t* paletteRow, std::uint32_t paletteRowIndex, std::uint32_t paletteGeneration, const void* palette)
	{
		if (_pixels.empty() || _uploadFormat != PixelFormat::RG8 || paletteRow == nullptr) {
			return nullptr;
		}
		const std::uint32_t currentFrame = GxDevice::GetFrameCounter();

		// Bakes that have gone unused for a while give their memory back: every copy costs as much as a
		// full RGBA8 texture, and a row only a menu screen needed would otherwise stay resident for the
		// texture's whole lifetime. Far older than anything the asynchronous FIFO could still read.
		constexpr std::uint32_t ReclaimAfterFrames = 300;
		for (std::int32_t i = 0; i < BakedSlotCount; i++) {
			if (_bakedSlots[i].Store != nullptr && currentFrame - _bakedSlots[i].LastUsedFrame > ReclaimAfterFrames) {
				free(_bakedSlots[i].Store);
				_bakedSlots[i].Store = nullptr;
				_bakedSlots[i].Valid = false;
			}
		}

		BakedSlot* slot = nullptr;
		for (std::int32_t i = 0; i < BakedSlotCount; i++) {
			if (_bakedSlots[i].Valid && _bakedSlots[i].PaletteRow == paletteRowIndex && _bakedSlots[i].Palette == palette) {
				if (_bakedSlots[i].PaletteGeneration == paletteGeneration && _bakedSlots[i].ContentVersion == _contentVersion) {
					_bakedSlots[i].LastUsedFrame = currentFrame;
					return &_bakedSlots[i].TexObj;
				}
				slot = &_bakedSlots[i];	// Stale bake of the same row, refresh it in place
				break;
			}
		}
		if (slot == nullptr) {
			for (std::int32_t i = 0; i < BakedSlotCount; i++) {
				if (!_bakedSlots[i].Valid) {
					slot = &_bakedSlots[i];
					break;
				}
			}
		}
		if (slot == nullptr) {
			// Never evict a bake the current frame still references - the FIFO consumes draws
			// asynchronously, so overwriting one could corrupt the already submitted quads
			for (std::int32_t i = 0; i < BakedSlotCount; i++) {
				const std::int32_t candidate = (_nextBakedSlot + i) % BakedSlotCount;
				if (_bakedSlots[candidate].LastUsedFrame != currentFrame) {
					slot = &_bakedSlots[candidate];
					_nextBakedSlot = (candidate + 1) % BakedSlotCount;
					break;
				}
			}
		}
		if (slot == nullptr) {
			static bool warnedSlots = false;
			if (!warnedSlots) {
				warnedSlots = true;
				LOGW("More than {} palette rows used with one texture in a single frame, expect glitches", BakedSlotCount);
			}
			slot = &_bakedSlots[_nextBakedSlot];
			_nextBakedSlot = (_nextBakedSlot + 1) % BakedSlotCount;
		}

		const std::size_t required = TiledSizeRgba8(_width, _height);
		if (slot->Store == nullptr) {
			slot->Store = static_cast<std::uint8_t*>(memalign(32, required));
			if (slot->Store == nullptr) {
				LOGE("Out of memory allocating a {} B baked texture store", required);
				return nullptr;
			}
		}

		// Resolve each texel through the palette row (index -> RGB) and its own alpha byte, into a
		// temporary linear RGBA row consumed by the tiler row by row
		std::vector<std::uint8_t> linear(std::size_t(_width) * std::size_t(_height) * 4);
		for (std::int32_t y = 0; y < _height; y++) {
			const std::uint8_t* src = _pixels.data() + std::size_t(y) * _strideBytes;
			std::uint8_t* dst = linear.data() + std::size_t(y) * _width * 4;
			for (std::int32_t x = 0; x < _width; x++) {
				const std::uint32_t color = paletteRow[src[x * 2]];
				dst[x * 4 + 0] = std::uint8_t(color & 0xFF);
				dst[x * 4 + 1] = std::uint8_t((color >> 8) & 0xFF);
				dst[x * 4 + 2] = std::uint8_t((color >> 16) & 0xFF);
				dst[x * 4 + 3] = src[x * 2 + 1];
			}
		}
		TileRgba8(slot->Store, linear.data(), _width, _height, _width * 4, 4);
		InitTexObj(slot->TexObj, slot->Store, GX_TF_RGBA8, false);
		DCFlushRange(slot->Store, std::uint32_t(required));
		GX_InvalidateTexAll();

		slot->Valid = true;
		slot->PaletteRow = paletteRowIndex;
		slot->PaletteGeneration = paletteGeneration;
		slot->ContentVersion = _contentVersion;
		slot->LastUsedFrame = currentFrame;
		slot->Palette = palette;
		return &slot->TexObj;
	}

	void GxTexture::SetRenderTarget(bool isRenderTarget)
	{
		_isRenderTarget = isRenderTarget;
		if (isRenderTarget && _width > 0 && _height > 0) {
			// The EFB copy writes a tiled RGBA8 image; allocate/refresh the destination store
			const std::size_t required = TiledSizeRgba8(_width, _height);
			if (_tiledStore == nullptr || _tiledStoreSize != required) {
				if (_tiledStore != nullptr) {
					free(_tiledStore);
				}
				_tiledStore = static_cast<std::uint8_t*>(memalign(32, required));
				_tiledStoreSize = required;
			}
			if (_tiledStore != nullptr) {
				InitTexObj(_texObj, _tiledStore, GX_TF_RGBA8, false);
				_texObjValid = true;
			}
		}
	}

	bool GxTexture::Bind(std::uint32_t textureUnit) const
	{
		_textureUnit = textureUnit;
		GxDevice::BindTexture(textureUnit, this);
		return true;
	}

	bool GxTexture::Unbind() const
	{
		GxDevice::BindTexture(_textureUnit, nullptr);
		return true;
	}

	bool GxTexture::Unbind(std::uint32_t textureUnit)
	{
		GxDevice::BindTexture(textureUnit, nullptr);
		return true;
	}

	void GxTexture::TexImage2D(std::int32_t level, PixelFormat format, bool bgr, std::int32_t width, std::int32_t height, const void* data)
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
			GxDevice::NotifyPaletteTextureChanged(this, 0, _height);
		} else {
			RefreshTiledStore();
		}
	}

	void GxTexture::TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data)
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
			GxDevice::NotifyPaletteTextureChanged(this, yoffset, height);
		} else {
			// v1 keeps the tiling simple: any sub-update re-tiles the whole level (sub-updates are rare -
			// tileset overrides and the palette, which is intercepted above)
			RefreshTiledStore();
		}
	}

	void GxTexture::TexStorage2D(std::int32_t levels, PixelFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(levels);
		Allocate(format, width, height);
		if (_isRenderTarget) {
			SetRenderTarget(true);
		}
	}

	void GxTexture::CompressedTexImage2D(std::int32_t level, PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t imageSize, const void* data)
	{
		static_cast<void>(level); static_cast<void>(format); static_cast<void>(width);
		static_cast<void>(height); static_cast<void>(imageSize); static_cast<void>(data);
	}

	void GxTexture::CompressedTexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, std::int32_t imageSize, const void* data)
	{
		static_cast<void>(level); static_cast<void>(xoffset); static_cast<void>(yoffset); static_cast<void>(width);
		static_cast<void>(height); static_cast<void>(format); static_cast<void>(imageSize); static_cast<void>(data);
	}

	void GxTexture::GetTexImage(std::int32_t level, PixelFormat format, bool bgr, void* pixels)
	{
		static_cast<void>(level);
		static_cast<void>(format);
		static_cast<void>(bgr);
		if (pixels != nullptr && !_pixels.empty()) {
			std::memcpy(pixels, _pixels.data(), _pixels.size());
		}
	}

	void GxTexture::SetMinFiltering(nCine::SamplerFilter filter)
	{
		_minFilter = filter;
		if (_texObjValid) {
			const std::uint8_t f = (_magFilter == nCine::SamplerFilter::Linear ? GX_LINEAR : GX_NEAR);
			GX_InitTexObjFilterMode(&_texObj, f, f);
		}
	}

	void GxTexture::SetMagFiltering(nCine::SamplerFilter filter)
	{
		_magFilter = filter;
		if (_texObjValid) {
			const std::uint8_t f = (_magFilter == nCine::SamplerFilter::Linear ? GX_LINEAR : GX_NEAR);
			GX_InitTexObjFilterMode(&_texObj, f, f);
		}
	}

	void GxTexture::SetWrap(SamplerWrapping wrap)
	{
		if (_wrap == wrap) {
			return;
		}
		_wrap = wrap;
		// Wrap only parameterizes GX_InitTexObj (read by InitTexObj), so the texture objects are
		// re-initialized in place over the existing stores - the texel data is unaffected, and re-tiling
		// the whole level through RefreshTiledStore() here was pure wasted work
		if (_texObjValid && _tiledStore != nullptr) {
			// Render targets always hold an RGBA8 store, whatever the upload format was (see SetRenderTarget)
			if (_uploadFormat == PixelFormat::R8 && !_isRenderTarget) {
				InitTexObj(_texObj, _tiledStore, GX_TF_CI8, true);
			} else {
				InitTexObj(_texObj, _tiledStore, GX_TF_RGBA8, false);
			}
		}
		// The baked RGBA8 copies of an RG8 store carry their own texture objects with the same wrap state
		for (std::int32_t i = 0; i < BakedSlotCount; i++) {
			if (_bakedSlots[i].Valid && _bakedSlots[i].Store != nullptr) {
				InitTexObj(_bakedSlots[i].TexObj, _bakedSlots[i].Store, GX_TF_RGBA8, false);
			}
		}
	}

	void GxTexture::SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a)
	{
		_swizzle[0] = r;
		_swizzle[1] = g;
		_swizzle[2] = b;
		_swizzle[3] = a;
	}

	void GxTexture::SetMaxLevel(std::int32_t maxLevel)
	{
		static_cast<void>(maxLevel);
	}

	void GxTexture::SetUnpackAlignment(std::int32_t alignment)
	{
		static_cast<void>(alignment);
	}

	void GxTexture::SetObjectLabel(StringView label)
	{
		// The shared palette texture is uploaded by ContentResolver under this exact name; its rows are
		// turned into TLUTs by the device instead of a sampled texture
		if (label == "Palettes"_s) {
			_isPaletteTexture = true;
			FreeTiledStores();
			GxDevice::RegisterPaletteTexture(this);
		}
	}
}
