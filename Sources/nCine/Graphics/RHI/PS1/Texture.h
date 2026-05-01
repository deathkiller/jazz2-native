#pragma once

#if defined(WITH_RHI_PS1)

#include "../RenderTypes.h"

#include <psxgpu.h>

#include <cstdint>
#include <memory>

namespace nCine::RHI
{
	// =========================================================================
	// Texture - wraps a VRAM allocation + texture-page + CLUT descriptor
	// =========================================================================
	class Texture
	{
	public:
		static constexpr std::int32_t MaxTextureUnitsConst = 1;

		Texture() = default;
		~Texture() = default;

		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;

		inline std::int32_t  GetWidth()    const { return width_;    }
		inline std::int32_t  GetHeight()   const { return height_;   }
		inline std::int32_t  GetMipCount() const { return 1;         }
		inline TextureFormat GetFormat()   const { return format_;   }
		inline SamplerFilter GetMinFilter() const { return minFilter_; }
		inline SamplerFilter GetMagFilter() const { return magFilter_; }
		inline SamplerWrapping GetWrapS()  const { return wrapS_;    }
		inline SamplerWrapping GetWrapT()  const { return wrapT_;    }

		/// Packed TPage word returned by getTPage(); set in POLY_FT4.tpage.
		inline std::uint16_t GetTPage()  const { return tpage_;  }
		/// Packed CLUT word returned by getClut(); 0 for 15-bit direct.
		inline std::uint16_t GetClut()   const { return clut_;   }
		/// Pixel format: 0=4bpp, 1=8bpp, 2=15bpp
		inline std::int32_t  GetMode()   const { return mode_;   }
		inline const VramRegion& GetRegion() const { return region_; }

		/// Upload pixel data to VRAM using DMA.
		/// Caller must have called InitVram() to set up region_ first.
		void UploadMip(std::int32_t mipLevel, std::int32_t width, std::int32_t height,
		               TextureFormat format, const void* data, std::size_t size)
		{
			if (mipLevel != 0 || data == nullptr) {
				return;
			}

			width_  = width;
			height_ = height;
			format_ = format;
			mode_   = ToNativeMode(format);

			// Copy to a staging buffer for VRAM upload
			staging_ = std::make_unique<std::uint8_t[]>(size);
			std::memcpy(staging_.get(), data, size);

			if (!region_.valid) {
				return; // VramRegion must be set before uploading
			}

			// Upload to VRAM via DMA
			RECT rect;
			setRECT(&rect, region_.x, region_.y, static_cast<short>(width), static_cast<short>(height));
			LoadImage(&rect, reinterpret_cast<const std::uint32_t*>(staging_.get()));
			DrawSync(0);

			// Compute texture page word — tx in 64-pixel steps, ty in 256-row steps
			tpage_ = static_cast<std::uint16_t>(getTPage(mode_, 0,
			    region_.x / 64, region_.y / 256));
		}

		/// Set VRAM region (must be called before UploadMip).
		void InitVram(std::int16_t x, std::int16_t y, std::int16_t w, std::int16_t h)
		{
			region_ = { x, y, w, h, true };
		}

		/// Set CLUT (colour look-up table) address for 4/8-bpp textures.
		void SetClut(std::int16_t cx, std::int16_t cy)
		{
			clut_ = static_cast<std::uint16_t>(getClut(cx, cy));
		}

		void SetFilter(SamplerFilter minFilter, SamplerFilter magFilter)
		{
			minFilter_ = minFilter;
			magFilter_ = magFilter;
		}

		void SetWrapping(SamplerWrapping wrapS, SamplerWrapping wrapT)
		{
			wrapS_ = wrapS;
			wrapT_ = wrapT;
		}

		/// PS1 pixel mode: 0 = 4bpp CLUT, 1 = 8bpp CLUT, 2 = 15-bit direct, 3 = 24-bit
		static std::int32_t ToNativeMode(TextureFormat fmt)
		{
			switch (fmt) {
				case TextureFormat::RGBA8:   return 2; // store as 15-bit direct (loses alpha precision)
				case TextureFormat::RGB8:    return 2;
				case TextureFormat::RGBA4:   return 0; // 4bpp CLUT
				case TextureFormat::RGB5A1:  return 2;
				case TextureFormat::R8:      return 1; // 8bpp CLUT
				default:                     return 2;
			}
		}

	private:
		std::unique_ptr<std::uint8_t[]> staging_;
		VramRegion      region_;
		std::int32_t    width_         = 0;
		std::int32_t    height_        = 0;
		TextureFormat   format_        = TextureFormat::Unknown;
		std::int32_t    mode_          = 2;     // default: 15-bit direct
		std::uint16_t   tpage_         = 0;
		std::uint16_t   clut_          = 0;
		SamplerFilter   minFilter_     = SamplerFilter::Nearest;
		SamplerFilter   magFilter_     = SamplerFilter::Nearest;
		SamplerWrapping wrapS_         = SamplerWrapping::ClampToEdge;
		SamplerWrapping wrapT_         = SamplerWrapping::ClampToEdge;
	};

}

#endif