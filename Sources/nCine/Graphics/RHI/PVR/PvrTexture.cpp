#include "PvrTexture.h"
#include "PvrDevice.h"

#include "../../../../Main.h"

#include <cstring>

using namespace Death::Containers::Literals;

namespace nCine::RHI::PVR
{
	namespace
	{
		// Rounds a texture dimension up to the next power of two within the PVR's [8, 1024] window
		std::int32_t NextPow2(std::int32_t value)
		{
			std::int32_t result = 8;
			while (result < value && result < 1024) {
				result <<= 1;
			}
			return result;
		}

		inline std::uint16_t Argb4444FromRgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
		{
			return std::uint16_t(((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
		}
	}

	std::uint32_t PvrTexture::nextHandle_ = 1;
	std::uint32_t PvrTexture::nextContentVersion_ = 0;

	PvrTexture::PvrTexture(TextureTarget target)
		: handle_(nextHandle_++), contentVersion_(0), target_(target), format_(PixelFormat::Unknown), uploadFormat_(PixelFormat::Unknown),
			width_(0), height_(0), strideBytes_(0), bytesPerPixel_(0),
			minFilter_(nCine::SamplerFilter::Nearest), magFilter_(nCine::SamplerFilter::Nearest), wrap_(SamplerWrapping::ClampToEdge),
			textureUnit_(0), isRenderTarget_(false), isPaletteTexture_(false),
			vram_(nullptr), vramFormat_(0), paddedWidth_(0), paddedHeight_(0), uScale_(1.0f), vScale_(1.0f),
			bakedSlots_{}, nextBakedSlot_(0)
	{
		swizzle_[0] = SwizzleChannel::Red;
		swizzle_[1] = SwizzleChannel::Green;
		swizzle_[2] = SwizzleChannel::Blue;
		swizzle_[3] = SwizzleChannel::Alpha;
	}

	PvrTexture::~PvrTexture()
	{
		PvrDevice::UnbindTexture(this);
		FreeVramStores();
	}

	void PvrTexture::FreeVramStores()
	{
		if (vram_ != nullptr) {
			pvr_mem_free(vram_);
			vram_ = nullptr;
		}
		for (std::int32_t i = 0; i < BakedSlotCount; i++) {
			if (bakedSlots_[i].Vram != nullptr) {
				pvr_mem_free(bakedSlots_[i].Vram);
				bakedSlots_[i].Vram = nullptr;
			}
			bakedSlots_[i].Valid = false;
		}
	}

	std::int32_t PvrTexture::BytesPerPixel(PixelFormat format)
	{
		switch (format) {
			case PixelFormat::R8: return 1;
			case PixelFormat::RG8: return 2;
			case PixelFormat::RGB8: return 3;
			case PixelFormat::RGBA8: return 4;
			default: return 0;
		}
	}

	void PvrTexture::Allocate(PixelFormat format, std::int32_t width, std::int32_t height)
	{
		uploadFormat_ = format;
		format_ = format;
		width_ = width;
		height_ = height;
		bytesPerPixel_ = BytesPerPixel(format_);
		strideBytes_ = width * bytesPerPixel_;
		pixels_.assign(std::size_t(strideBytes_) * std::size_t(height > 0 ? height : 0), std::uint8_t(0));
		FreeVramStores();
		paddedWidth_ = NextPow2(width);
		paddedHeight_ = NextPow2(height);
		uScale_ = (paddedWidth_ > 0 ? float(width) / float(paddedWidth_) : 1.0f);
		vScale_ = (paddedHeight_ > 0 ? float(height) / float(paddedHeight_) : 1.0f);
		if (width > 1024 || height > 1024) {
			LOGE("Texture {}x{} exceeds the PowerVR 1024 limit", width, height);
		}
		contentVersion_ = ++nextContentVersion_;
	}

	void PvrTexture::RefreshVramStore()
	{
		if (pixels_.empty() || width_ <= 0 || height_ <= 0 || isPaletteTexture_ || isRenderTarget_) {
			return;
		}

		if (uploadFormat_ == PixelFormat::R8) {
			// 8bpp paletted, twiddled; the palette bank is or-ed into the format word per draw
			const std::size_t size = std::size_t(paddedWidth_) * std::size_t(paddedHeight_);
			if (vram_ == nullptr) {
				vram_ = pvr_mem_malloc(size);
				if (vram_ == nullptr) {
					LOGE("Out of PVR memory allocating {} B (8bpp {}x{})", size, paddedWidth_, paddedHeight_);
					return;
				}
			}
			// Pad the linear image into a power-of-two staging buffer, then let the loader twiddle it
			std::vector<std::uint8_t> staging(size, 0);
			for (std::int32_t y = 0; y < height_; y++) {
				std::memcpy(staging.data() + std::size_t(y) * paddedWidth_, pixels_.data() + std::size_t(y) * strideBytes_, std::size_t(width_));
			}
			pvr_txr_load_ex(staging.data(), vram_, std::uint32_t(paddedWidth_), std::uint32_t(paddedHeight_), PVR_TXRLOAD_8BPP);
			vramFormat_ = PVR_TXRFMT_PAL8BPP | PVR_TXRFMT_TWIDDLED;
		} else if (uploadFormat_ == PixelFormat::RGB8 || uploadFormat_ == PixelFormat::RGBA8) {
			// True-color converts to twiddled ARGB4444 (the PVR has no 32-bit sampled format)
			const std::size_t size = std::size_t(paddedWidth_) * std::size_t(paddedHeight_) * 2;
			if (vram_ == nullptr) {
				vram_ = pvr_mem_malloc(size);
				if (vram_ == nullptr) {
					LOGE("Out of PVR memory allocating {} B (ARGB4444 {}x{})", size, paddedWidth_, paddedHeight_);
					return;
				}
			}
			std::vector<std::uint16_t> staging(std::size_t(paddedWidth_) * std::size_t(paddedHeight_), 0);
			for (std::int32_t y = 0; y < height_; y++) {
				const std::uint8_t* src = pixels_.data() + std::size_t(y) * strideBytes_;
				std::uint16_t* dst = staging.data() + std::size_t(y) * paddedWidth_;
				for (std::int32_t x = 0; x < width_; x++) {
					const std::uint8_t* px = src + std::size_t(x) * bytesPerPixel_;
					dst[x] = Argb4444FromRgba(px[0], px[1], px[2], (bytesPerPixel_ >= 4 ? px[3] : std::uint8_t(255)));
				}
			}
			pvr_txr_load_ex(staging.data(), vram_, std::uint32_t(paddedWidth_), std::uint32_t(paddedHeight_), PVR_TXRLOAD_16BPP);
			vramFormat_ = PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_TWIDDLED;
		}
		// RG8 keeps only the linear store; the ARGB4444 copy is baked per palette row on demand
	}

	pvr_ptr_t PvrTexture::EnsureBakedArgb4444(const std::uint32_t* paletteRow, std::uint32_t paletteRowIndex, std::uint32_t paletteGeneration, const void* palette)
	{
		if (pixels_.empty() || uploadFormat_ != PixelFormat::RG8 || paletteRow == nullptr) {
			return nullptr;
		}
		const std::uint32_t currentScene = PvrDevice::GetSceneCounter();
		BakedSlot* slot = nullptr;
		for (std::int32_t i = 0; i < BakedSlotCount; i++) {
			if (bakedSlots_[i].Valid && bakedSlots_[i].PaletteRow == paletteRowIndex && bakedSlots_[i].Palette == palette) {
				if (bakedSlots_[i].PaletteGeneration == paletteGeneration && bakedSlots_[i].ContentVersion == contentVersion_) {
					bakedSlots_[i].LastUsedScene = currentScene;
					return bakedSlots_[i].Vram;
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
			// Never evict a bake the current scene still references - the tile accelerator reads the
			// textures only at scene end, so overwriting one would corrupt the already submitted quads
			for (std::int32_t i = 0; i < BakedSlotCount; i++) {
				const std::int32_t candidate = (nextBakedSlot_ + i) % BakedSlotCount;
				if (bakedSlots_[candidate].LastUsedScene != currentScene) {
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
				LOGW("More than {} palette rows used with one texture in a single scene, expect glitches", BakedSlotCount);
			}
			slot = &bakedSlots_[nextBakedSlot_];
			nextBakedSlot_ = (nextBakedSlot_ + 1) % BakedSlotCount;
		}

		const std::size_t size = std::size_t(paddedWidth_) * std::size_t(paddedHeight_) * 2;
		if (slot->Vram == nullptr) {
			slot->Vram = pvr_mem_malloc(size);
			if (slot->Vram == nullptr) {
				LOGE("Out of PVR memory allocating {} B (baked ARGB4444 {}x{})", size, paddedWidth_, paddedHeight_);
				return nullptr;
			}
		}

		std::vector<std::uint16_t> staging(std::size_t(paddedWidth_) * std::size_t(paddedHeight_), 0);
		for (std::int32_t y = 0; y < height_; y++) {
			const std::uint8_t* src = pixels_.data() + std::size_t(y) * strideBytes_;
			std::uint16_t* dst = staging.data() + std::size_t(y) * paddedWidth_;
			for (std::int32_t x = 0; x < width_; x++) {
				const std::uint32_t color = paletteRow[src[x * 2]];
				dst[x] = Argb4444FromRgba(std::uint8_t(color & 0xFF), std::uint8_t((color >> 8) & 0xFF),
					std::uint8_t((color >> 16) & 0xFF), src[x * 2 + 1]);
			}
		}
		pvr_txr_load_ex(staging.data(), slot->Vram, std::uint32_t(paddedWidth_), std::uint32_t(paddedHeight_), PVR_TXRLOAD_16BPP);

		slot->Valid = true;
		slot->PaletteRow = paletteRowIndex;
		slot->PaletteGeneration = paletteGeneration;
		slot->ContentVersion = contentVersion_;
		slot->LastUsedScene = currentScene;
		slot->Palette = palette;
		return slot->Vram;
	}

	void PvrTexture::SetRenderTarget(bool isRenderTarget)
	{
		isRenderTarget_ = isRenderTarget;
		if (isRenderTarget && width_ > 0 && height_ > 0) {
			// The tile accelerator renders into a non-twiddled RGB565 surface of power-of-two width
			const std::size_t size = std::size_t(paddedWidth_) * std::size_t(paddedHeight_) * 2;
			if (vram_ == nullptr) {
				vram_ = pvr_mem_malloc(size);
				if (vram_ == nullptr) {
					LOGE("Out of PVR memory allocating {} B (RTT {}x{})", size, paddedWidth_, paddedHeight_);
					return;
				}
			}
			vramFormat_ = PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED;
		}
	}

	bool PvrTexture::Bind(std::uint32_t textureUnit) const
	{
		textureUnit_ = textureUnit;
		PvrDevice::BindTexture(textureUnit, this);
		return true;
	}

	bool PvrTexture::Unbind() const
	{
		PvrDevice::BindTexture(textureUnit_, nullptr);
		return true;
	}

	bool PvrTexture::Unbind(std::uint32_t textureUnit)
	{
		PvrDevice::BindTexture(textureUnit, nullptr);
		return true;
	}

	void PvrTexture::TexImage2D(std::int32_t level, PixelFormat format, bool bgr, std::int32_t width, std::int32_t height, const void* data)
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
			PvrDevice::NotifyPaletteTextureChanged(this, 0, height_);
		} else {
			RefreshVramStore();
		}
	}

	void PvrTexture::TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data)
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
			PvrDevice::NotifyPaletteTextureChanged(this, yoffset, height);
		} else {
			// v1: any sub-update re-uploads the whole level (sub-updates are rare - tileset overrides and
			// the palette, which is intercepted above)
			RefreshVramStore();
		}
	}

	void PvrTexture::TexStorage2D(std::int32_t levels, PixelFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(levels);
		Allocate(format, width, height);
		if (isRenderTarget_) {
			SetRenderTarget(true);
		}
	}

	void PvrTexture::CompressedTexImage2D(std::int32_t level, PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t imageSize, const void* data)
	{
		static_cast<void>(level); static_cast<void>(format); static_cast<void>(width);
		static_cast<void>(height); static_cast<void>(imageSize); static_cast<void>(data);
	}

	void PvrTexture::CompressedTexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, std::int32_t imageSize, const void* data)
	{
		static_cast<void>(level); static_cast<void>(xoffset); static_cast<void>(yoffset); static_cast<void>(width);
		static_cast<void>(height); static_cast<void>(format); static_cast<void>(imageSize); static_cast<void>(data);
	}

	void PvrTexture::GetTexImage(std::int32_t level, PixelFormat format, bool bgr, void* pixels)
	{
		static_cast<void>(level);
		static_cast<void>(format);
		static_cast<void>(bgr);
		if (pixels != nullptr && !pixels_.empty()) {
			std::memcpy(pixels, pixels_.data(), pixels_.size());
		}
	}

	void PvrTexture::SetMinFiltering(nCine::SamplerFilter filter)
	{
		minFilter_ = filter;
	}

	void PvrTexture::SetMagFiltering(nCine::SamplerFilter filter)
	{
		magFilter_ = filter;
	}

	void PvrTexture::SetWrap(SamplerWrapping wrap)
	{
		wrap_ = wrap;
	}

	void PvrTexture::SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a)
	{
		swizzle_[0] = r;
		swizzle_[1] = g;
		swizzle_[2] = b;
		swizzle_[3] = a;
	}

	void PvrTexture::SetMaxLevel(std::int32_t maxLevel)
	{
		static_cast<void>(maxLevel);
	}

	void PvrTexture::SetUnpackAlignment(std::int32_t alignment)
	{
		static_cast<void>(alignment);
	}

	void PvrTexture::SetObjectLabel(StringView label)
	{
		// The shared palette texture is uploaded by ContentResolver under this exact name; its rows are
		// loaded into the hardware palette banks by the device instead of a sampled texture
		if (label == "Palettes"_s) {
			isPaletteTexture_ = true;
			FreeVramStores();
			PvrDevice::RegisterPaletteTexture(this);
		}
	}
}
