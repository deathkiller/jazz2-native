#pragma once

#if defined(WITH_RHI_SAT)

#include "../RenderTypes.h"

// Saturn VDP2 headers

#include <cstdint>
#include <memory>

namespace nCine::RHI
{
	// =========================================================================
	// Texture - wraps a VDP1 VRAM allocation for textured sprites
	//
	// VDP1 textures must be stored in VDP1 VRAM (0x25C00000..0x25C7FFFF).
	// The address is expressed as a character base address (offset / 8).
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

		/// VDP1 character base address (byte offset into VDP1 VRAM / 8).
		inline std::uint32_t GetCharBase()    const { return charBase_;    }
		/// VDP1 color mode: 0=4bpp, 1=4bpp+highlight, 2=8bpp, 3=15bpp+shadow, 4=15bpp direct
		inline std::int32_t  GetColorMode()   const { return colorMode_;  }
		/// CLUT address (word offset in CRAM for palettised modes).
		inline std::uint16_t GetClutOffset()  const { return clutOffset_; }
		/// Pixel data pointer for uploading to VDP1 VRAM.
		inline const void*   GetPixelData()   const { return pixelData_.get(); }
		inline std::size_t   GetPixelDataSize() const { return dataSize_; }

		void UploadMip(std::int32_t mipLevel, std::int32_t width, std::int32_t height,
		               TextureFormat format, const void* data, std::size_t size)
		{
			if (mipLevel != 0 || data == nullptr) {
				return;
			}

			width_     = width;
			height_    = height;
			format_    = format;
			colorMode_ = ToColorMode(format);

			pixelData_ = std::make_unique<std::uint8_t[]>(size);
			std::memcpy(pixelData_.get(), data, size);
			dataSize_  = size;

			// Write pixel data to VDP1 VRAM if charBase_ has been assigned
			if (charBase_ != 0) {
				std::uint8_t* vramDst = reinterpret_cast<std::uint8_t*>(VDP1_VRAM(charBase_ * 8));
				std::memcpy(vramDst, pixelData_.get(), dataSize_);
			}
		}

		/// Set VDP1 VRAM address (must be called before UploadMip or after).
		void SetVramAddress(std::uint32_t charBase)
		{
			charBase_ = charBase;
			if (pixelData_ != nullptr && charBase_ != 0) {
				std::uint8_t* vramDst = reinterpret_cast<std::uint8_t*>(VDP1_VRAM(charBase_ * 8));
				std::memcpy(vramDst, pixelData_.get(), dataSize_);
			}
		}

		void SetClutOffset(std::uint16_t clutOffset)
		{
			clutOffset_ = clutOffset;
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

		/// VDP1 color mode from Rhi TextureFormat.
		static std::int32_t ToColorMode(TextureFormat fmt)
		{
			switch (fmt) {
				case TextureFormat::RGBA4:   return 0;  // 4bpp CLUT
				case TextureFormat::R8:
				case TextureFormat::RG8:     return 2;  // 8bpp CLUT
				case TextureFormat::RGB8:
				case TextureFormat::RGBA8:   return 4;  // 15bpp direct
				case TextureFormat::RGB5A1:  return 3;  // 15bpp with shadow
				default:                     return 4;
			}
		}

	private:
		std::unique_ptr<std::uint8_t[]> pixelData_;
		std::size_t     dataSize_     = 0;
		std::int32_t    width_        = 0;
		std::int32_t    height_       = 0;
		TextureFormat   format_       = TextureFormat::Unknown;
		std::int32_t    colorMode_    = 4;
		std::uint32_t   charBase_     = 0;     // VDP1 VRAM character base (byte_offset / 8)
		std::uint16_t   clutOffset_   = 0;
		SamplerFilter   minFilter_    = SamplerFilter::Nearest;
		SamplerFilter   magFilter_    = SamplerFilter::Nearest;
		SamplerWrapping wrapS_        = SamplerWrapping::ClampToEdge;
		SamplerWrapping wrapT_        = SamplerWrapping::ClampToEdge;
	};

}

#endif