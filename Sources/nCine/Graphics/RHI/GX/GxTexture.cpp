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

	std::uint32_t GxTexture::nextHandle_ = 1;
	std::uint32_t GxTexture::nextContentVersion_ = 0;

	GxTexture::GxTexture(TextureTarget target)
		: handle_(nextHandle_++), contentVersion_(0), target_(target), format_(PixelFormat::Unknown), uploadFormat_(PixelFormat::Unknown),
			width_(0), height_(0), strideBytes_(0), bytesPerPixel_(0),
			minFilter_(nCine::SamplerFilter::Nearest), magFilter_(nCine::SamplerFilter::Nearest), wrap_(SamplerWrapping::ClampToEdge),
			textureUnit_(0), isRenderTarget_(false), isPaletteTexture_(false),
			tiledStore_(nullptr), tiledStoreSize_(0), texObjValid_(false),
			bakedSlots_{}, nextBakedSlot_(0)
	{
		swizzle_[0] = SwizzleChannel::Red;
		swizzle_[1] = SwizzleChannel::Green;
		swizzle_[2] = SwizzleChannel::Blue;
		swizzle_[3] = SwizzleChannel::Alpha;
	}

	GxTexture::~GxTexture()
	{
		// Clear from the device so a destroyed texture can't dangle in the bound-texture table
		GxDevice::UnbindTexture(this);
		FreeTiledStores();
	}

	void GxTexture::FreeTiledStores()
	{
		if (tiledStore_ != nullptr) {
			free(tiledStore_);
			tiledStore_ = nullptr;
			tiledStoreSize_ = 0;
		}
		texObjValid_ = false;
		for (std::int32_t i = 0; i < BakedSlotCount; i++) {
			if (bakedSlots_[i].Store != nullptr) {
				free(bakedSlots_[i].Store);
				bakedSlots_[i].Store = nullptr;
			}
			bakedSlots_[i].Valid = false;
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
		uploadFormat_ = format;
		format_ = format;
		width_ = width;
		height_ = height;
		bytesPerPixel_ = BytesPerPixel(format_);
		strideBytes_ = width * bytesPerPixel_;
		pixels_.assign(std::size_t(strideBytes_) * std::size_t(height > 0 ? height : 0), std::uint8_t(0));
		FreeTiledStores();
		contentVersion_ = ++nextContentVersion_;
	}

	void GxTexture::InitTexObj(GXTexObj& obj, void* store, std::uint8_t gxFormat, bool ci)
	{
		const std::uint8_t wrap = (wrap_ == SamplerWrapping::Repeat ? GX_REPEAT
			: (wrap_ == SamplerWrapping::MirroredRepeat ? GX_MIRROR : GX_CLAMP));
		if (ci) {
			// The TLUT name is patched per draw by the device (GX_InitTexObjTlut) before loading the object
			GX_InitTexObjCI(&obj, store, std::uint16_t(width_), std::uint16_t(height_), gxFormat, wrap, wrap, GX_FALSE, GX_TLUT0);
		} else {
			GX_InitTexObj(&obj, store, std::uint16_t(width_), std::uint16_t(height_), gxFormat, wrap, wrap, GX_FALSE);
		}
		const std::uint8_t filter = (magFilter_ == nCine::SamplerFilter::Linear ? GX_LINEAR : GX_NEAR);
		GX_InitTexObjFilterMode(&obj, filter, filter);
	}

	void GxTexture::RefreshTiledStore()
	{
		texObjValid_ = false;
		if (pixels_.empty() || width_ <= 0 || height_ <= 0 || isPaletteTexture_) {
			return;
		}

		std::size_t required = 0;
		if (uploadFormat_ == PixelFormat::R8) {
			required = TiledSizeCi8(width_, height_);
		} else if (uploadFormat_ == PixelFormat::RGB8 || uploadFormat_ == PixelFormat::RGBA8) {
			required = TiledSizeRgba8(width_, height_);
		} else {
			// RG8 keeps only the linear store; the tiled copy is baked per palette row on demand
			return;
		}

		if (tiledStore_ == nullptr || tiledStoreSize_ != required) {
			if (tiledStore_ != nullptr) {
				free(tiledStore_);
			}
			tiledStore_ = static_cast<std::uint8_t*>(memalign(32, required));
			tiledStoreSize_ = required;
			if (tiledStore_ == nullptr) {
				LOGE("Out of memory allocating a {} B tiled texture store", required);
				return;
			}
		}

		if (uploadFormat_ == PixelFormat::R8) {
			TileCi8(tiledStore_, pixels_.data(), width_, height_, strideBytes_);
			InitTexObj(texObj_, tiledStore_, GX_TF_CI8, true);
		} else {
			TileRgba8(tiledStore_, pixels_.data(), width_, height_, strideBytes_, bytesPerPixel_);
			InitTexObj(texObj_, tiledStore_, GX_TF_RGBA8, false);
		}
		DCFlushRange(tiledStore_, std::uint32_t(tiledStoreSize_));
		GX_InvalidateTexAll();
		texObjValid_ = true;
	}

	GXTexObj* GxTexture::GetTexObj()
	{
		if (!texObjValid_ && isRenderTarget_) {
			// A render target's tiled store is written by EFB copies; (re)create it lazily
			SetRenderTarget(true);
		}
		return (texObjValid_ ? &texObj_ : nullptr);
	}

	GXTexObj* GxTexture::EnsureBakedRgba(const std::uint32_t* paletteRow, std::uint32_t paletteRowIndex, std::uint32_t paletteGeneration)
	{
		if (pixels_.empty() || uploadFormat_ != PixelFormat::RG8 || paletteRow == nullptr) {
			return nullptr;
		}
		const std::uint32_t currentFrame = GxDevice::GetFrameCounter();
		BakedSlot* slot = nullptr;
		for (std::int32_t i = 0; i < BakedSlotCount; i++) {
			if (bakedSlots_[i].Valid && bakedSlots_[i].PaletteRow == paletteRowIndex) {
				if (bakedSlots_[i].PaletteGeneration == paletteGeneration && bakedSlots_[i].ContentVersion == contentVersion_) {
					bakedSlots_[i].LastUsedFrame = currentFrame;
					return &bakedSlots_[i].TexObj;
				}
				slot = &bakedSlots_[i];	// Stale bake of the same row, refresh it in place
				break;
			}
		}
		if (slot == nullptr) {
			for (std::int32_t i = 0; i < BakedSlotCount; i++) {
				if (!bakedSlots_[i].Valid) {
					slot = &bakedSlots_[i];
					break;
				}
			}
		}
		if (slot == nullptr) {
			// Never evict a bake the current frame still references - the FIFO consumes draws
			// asynchronously, so overwriting one could corrupt the already submitted quads
			for (std::int32_t i = 0; i < BakedSlotCount; i++) {
				const std::int32_t candidate = (nextBakedSlot_ + i) % BakedSlotCount;
				if (bakedSlots_[candidate].LastUsedFrame != currentFrame) {
					slot = &bakedSlots_[candidate];
					nextBakedSlot_ = (candidate + 1) % BakedSlotCount;
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
			slot = &bakedSlots_[nextBakedSlot_];
			nextBakedSlot_ = (nextBakedSlot_ + 1) % BakedSlotCount;
		}

		const std::size_t required = TiledSizeRgba8(width_, height_);
		if (slot->Store == nullptr) {
			slot->Store = static_cast<std::uint8_t*>(memalign(32, required));
			if (slot->Store == nullptr) {
				LOGE("Out of memory allocating a {} B baked texture store", required);
				return nullptr;
			}
		}

		// Resolve each texel through the palette row (index -> RGB) and its own alpha byte, into a
		// temporary linear RGBA row consumed by the tiler row by row
		std::vector<std::uint8_t> linear(std::size_t(width_) * std::size_t(height_) * 4);
		for (std::int32_t y = 0; y < height_; y++) {
			const std::uint8_t* src = pixels_.data() + std::size_t(y) * strideBytes_;
			std::uint8_t* dst = linear.data() + std::size_t(y) * width_ * 4;
			for (std::int32_t x = 0; x < width_; x++) {
				const std::uint32_t color = paletteRow[src[x * 2]];
				dst[x * 4 + 0] = std::uint8_t(color & 0xFF);
				dst[x * 4 + 1] = std::uint8_t((color >> 8) & 0xFF);
				dst[x * 4 + 2] = std::uint8_t((color >> 16) & 0xFF);
				dst[x * 4 + 3] = src[x * 2 + 1];
			}
		}
		TileRgba8(slot->Store, linear.data(), width_, height_, width_ * 4, 4);
		InitTexObj(slot->TexObj, slot->Store, GX_TF_RGBA8, false);
		DCFlushRange(slot->Store, std::uint32_t(required));
		GX_InvalidateTexAll();

		slot->Valid = true;
		slot->PaletteRow = paletteRowIndex;
		slot->PaletteGeneration = paletteGeneration;
		slot->ContentVersion = contentVersion_;
		slot->LastUsedFrame = currentFrame;
		return &slot->TexObj;
	}

	void GxTexture::SetRenderTarget(bool isRenderTarget)
	{
		isRenderTarget_ = isRenderTarget;
		if (isRenderTarget && width_ > 0 && height_ > 0) {
			// The EFB copy writes a tiled RGBA8 image; allocate/refresh the destination store
			const std::size_t required = TiledSizeRgba8(width_, height_);
			if (tiledStore_ == nullptr || tiledStoreSize_ != required) {
				if (tiledStore_ != nullptr) {
					free(tiledStore_);
				}
				tiledStore_ = static_cast<std::uint8_t*>(memalign(32, required));
				tiledStoreSize_ = required;
			}
			if (tiledStore_ != nullptr) {
				InitTexObj(texObj_, tiledStore_, GX_TF_RGBA8, false);
				texObjValid_ = true;
			}
		}
	}

	bool GxTexture::Bind(std::uint32_t textureUnit) const
	{
		textureUnit_ = textureUnit;
		GxDevice::BindTexture(textureUnit, this);
		return true;
	}

	bool GxTexture::Unbind() const
	{
		GxDevice::BindTexture(textureUnit_, nullptr);
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
		if (data != nullptr && !pixels_.empty()) {
			std::memcpy(pixels_.data(), data, pixels_.size());
			contentVersion_ = ++nextContentVersion_;
		}
		if (isPaletteTexture_) {
			GxDevice::NotifyPaletteTextureChanged(this, 0, height_);
		} else {
			RefreshTiledStore();
		}
	}

	void GxTexture::TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0 || data == nullptr || pixels_.empty()) {
			return;
		}
		const std::int32_t srcBpp = BytesPerPixel(format);
		const std::int32_t dstBpp = bytesPerPixel_;
		const std::int32_t copyBpp = (srcBpp < dstBpp ? srcBpp : dstBpp);
		for (std::int32_t y = 0; y < height; y++) {
			const std::int32_t dstY = yoffset + y;
			if (dstY < 0 || dstY >= height_) {
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
			if (dstX + copyW > width_) {
				copyW = width_ - dstX;
			}
			if (copyW <= 0) {
				continue;
			}
			const std::uint8_t* srcRow = static_cast<const std::uint8_t*>(data) + std::size_t(y) * std::size_t(width) * srcBpp + std::size_t(srcX0) * srcBpp;
			std::uint8_t* dstRow = pixels_.data() + std::size_t(dstY) * strideBytes_ + std::size_t(dstX) * dstBpp;
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
		contentVersion_ = ++nextContentVersion_;
		if (isPaletteTexture_) {
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
		if (isRenderTarget_) {
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
		if (pixels != nullptr && !pixels_.empty()) {
			std::memcpy(pixels, pixels_.data(), pixels_.size());
		}
	}

	void GxTexture::SetMinFiltering(nCine::SamplerFilter filter)
	{
		minFilter_ = filter;
		if (texObjValid_) {
			const std::uint8_t f = (magFilter_ == nCine::SamplerFilter::Linear ? GX_LINEAR : GX_NEAR);
			GX_InitTexObjFilterMode(&texObj_, f, f);
		}
	}

	void GxTexture::SetMagFiltering(nCine::SamplerFilter filter)
	{
		magFilter_ = filter;
		if (texObjValid_) {
			const std::uint8_t f = (magFilter_ == nCine::SamplerFilter::Linear ? GX_LINEAR : GX_NEAR);
			GX_InitTexObjFilterMode(&texObj_, f, f);
		}
	}

	void GxTexture::SetWrap(SamplerWrapping wrap)
	{
		wrap_ = wrap;
		if (texObjValid_) {
			RefreshTiledStore();
		}
	}

	void GxTexture::SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a)
	{
		swizzle_[0] = r;
		swizzle_[1] = g;
		swizzle_[2] = b;
		swizzle_[3] = a;
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
			isPaletteTexture_ = true;
			FreeTiledStores();
			GxDevice::RegisterPaletteTexture(this);
		}
	}
}
