#pragma once

#if defined(WITH_RHI_DC)

#include "../RenderTypes.h"

#include <kos.h>
#include <dc/pvr.h>

#include <cstdint>
#include <memory>

namespace nCine::RHI
{
	// =========================================================================
	// Texture - wraps a VRAM allocation via pvr_ptr_t
	//
	// The PVR hardware only supports power-of-2 textures in twiddled format.
	// Callers must ensure dimensions satisfy these constraints.
	// =========================================================================
	class Texture
	{
	public:
		static constexpr std::int32_t MaxTextureUnitsConst = 1; // PVR: 1 texture unit

		Texture() = default;

		~Texture()
		{
			FreeVram();
		}

		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;

		inline std::int32_t  GetWidth()    const { return width_;    }
		inline std::int32_t  GetHeight()   const { return height_;   }
		inline std::int32_t  GetMipCount() const { return mipCount_; }
		inline TextureFormat GetFormat()   const { return format_;   }
		inline SamplerFilter GetMinFilter() const { return minFilter_; }
		inline SamplerFilter GetMagFilter() const { return magFilter_; }
		inline SamplerWrapping GetWrapS()  const { return wrapS_;    }
		inline SamplerWrapping GetWrapT()  const { return wrapT_;    }

		/// Returns the VRAM texture pointer (may be nullptr if not yet uploaded).
		inline pvr_ptr_t GetVramPtr() const { return vram_; }

		/// Returns the native PVR texture format flag (PVR_TXRFMT_*).
		inline std::uint32_t GetNativeFormat() const { return nativeFormat_; }

		/// Upload texture mip data to VRAM.
		/// @param mipLevel  0 = base, 1+ = lower mips
		/// @param width     Width of this mip level (must be power-of-2)
		/// @param height    Height of this mip level (must be power-of-2)
		/// @param format    Rhi texture format
		/// @param data      Source pixel data in the native PVR format
		/// @param size      Byte size of @p data
		void UploadMip(std::int32_t mipLevel, std::int32_t width, std::int32_t height,
		               TextureFormat format, const void* data, std::size_t size)
		{
			if (mipLevel == 0) {
				// Allocate VRAM on first upload or if size changes
				std::uint32_t nativeFmt = ToNativeFormat(format);
				std::size_t vramSize = static_cast<std::size_t>(width * height) * BytesPerPixel(nativeFmt);
				if (vram_ == nullptr || width_ != width || height_ != height || nativeFormat_ != nativeFmt) {
					FreeVram();
					vram_ = pvr_mem_malloc(vramSize);
				}
				width_        = width;
				height_       = height;
				format_       = format;
				nativeFormat_ = nativeFmt;
				mipCount_     = 1;
			} else {
				mipCount_ = mipLevel + 1;
			}

			if (vram_ != nullptr && data != nullptr) {
				// pvr_txr_load_ex handles twiddling and DMA to VRAM
				pvr_txr_load_ex(const_cast<void*>(data), vram_, static_cast<uint32>(size), PVR_TXRLOAD_INVERT_Y);
			}
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

		/// Translates Rhi TextureFormat to a PVR_TXRFMT_* constant.
		static std::uint32_t ToNativeFormat(TextureFormat format)
		{
			switch (format) {
				case TextureFormat::RGBA8:   return PVR_TXRFMT_ARGB4444; // lossy, closest to RGBA8
				case TextureFormat::RGB8:    return PVR_TXRFMT_RGB565;
				case TextureFormat::RG8:     return PVR_TXRFMT_RGB565;   // R in R, G in G, B=0
				case TextureFormat::R8:      return PVR_TXRFMT_RGB565;
				case TextureFormat::RGBA4:   return PVR_TXRFMT_ARGB4444;
				case TextureFormat::RGB5A1:  return PVR_TXRFMT_ARGB1555;
				case TextureFormat::R16F:    return PVR_TXRFMT_RGB565;
				case TextureFormat::RG16F:   return PVR_TXRFMT_RGB565;
				case TextureFormat::RGBA16F: return PVR_TXRFMT_ARGB4444;
				case TextureFormat::R32F:    return PVR_TXRFMT_RGB565;
				default:                     return PVR_TXRFMT_ARGB4444;
			}
		}

		/// Returns the bytes per pixel for a given PVR_TXRFMT_* constant.
		static std::size_t BytesPerPixel(std::uint32_t nativeFormat)
		{
			// All PVR 2D texture formats are 16-bit (2 bytes) except palettized
			(void)nativeFormat;
			return 2;
		}

		/// Returns the PVR filter constant matching a SamplerFilter.
		static int ToNativeFilter(SamplerFilter filter)
		{
			switch (filter) {
				case SamplerFilter::Linear:
				case SamplerFilter::LinearMipmapLinear:
				case SamplerFilter::LinearMipmapNearest:
					return PVR_FILTER_BILINEAR;
				default:
					return PVR_FILTER_NEAREST;
			}
		}

		/// Returns the PVR clamp constant matching a SamplerWrapping.
		static int ToNativeClamp(SamplerWrapping wrapping)
		{
			return (wrapping == SamplerWrapping::ClampToEdge) ? PVR_UVCLAMP_UV : PVR_UVCLAMP_NONE;
		}

	private:
		void FreeVram()
		{
			if (vram_ != nullptr) {
				pvr_mem_free(vram_);
				vram_ = nullptr;
			}
		}

		pvr_ptr_t      vram_        = nullptr;
		std::uint32_t  nativeFormat_= PVR_TXRFMT_ARGB4444;
		std::int32_t   width_       = 0;
		std::int32_t   height_      = 0;
		std::int32_t   mipCount_    = 0;
		TextureFormat  format_      = TextureFormat::Unknown;
		SamplerFilter  minFilter_   = SamplerFilter::Nearest;
		SamplerFilter  magFilter_   = SamplerFilter::Nearest;
		SamplerWrapping wrapS_      = SamplerWrapping::ClampToEdge;
		SamplerWrapping wrapT_      = SamplerWrapping::ClampToEdge;
	};

}

#endif