#pragma once

#if defined(WITH_RHI_N64)

#include "../RenderTypes.h"

#include <libdragon.h>

#include <cstdint>
#include <memory>

namespace nCine::RHI
{
	// =========================================================================
	// Texture - wraps a libdragon sprite / surface in RDRAM + TMEM
	//
	// The RDP uses TMEM (4 KB texture memory tile cache) with hardware-managed
	// loading via LOAD_TILE.  Textures must be power-of-2 in each dimension.
	// The rdpq API handles TMEM management automatically per draw call.
	// =========================================================================
	class Texture
	{
	public:
		static constexpr std::int32_t MaxTextureUnitsConst = 1; // RDP: 1 texture unit (2 interleaved tiles possible but uncommon)

		Texture() = default;

		~Texture()
		{
			FreeData();
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

		/// Returns the raw RDRAM pixel data pointer (for rdpq_tex_upload).
		inline const void* GetPixelData() const { return pixelData_.get(); }
		inline tex_format_t GetNativeFormat() const { return nativeFormat_; }

		/// Allocate / re-upload texture data.
		/// @p data must be in the native format for this texture.
		void UploadMip(std::int32_t mipLevel, std::int32_t width, std::int32_t height,
		               TextureFormat format, const void* data, std::size_t size)
		{
			if (mipLevel == 0) {
				tex_format_t nativeFmt = ToNativeFormat(format);
				std::size_t allocSize = static_cast<std::size_t>(width * height) * BytesPerPixel(nativeFmt);
				if (width_ != width || height_ != height || nativeFormat_ != nativeFmt) {
					FreeData();
					pixelData_ = std::make_unique<std::uint8_t[]>(allocSize);
				}
				width_        = width;
				height_       = height;
				format_       = format;
				nativeFormat_ = nativeFmt;
				mipCount_     = 1;
				dataSize_     = allocSize;
			} else {
				mipCount_ = mipLevel + 1;
			}

			if (pixelData_ != nullptr && data != nullptr) {
				std::memcpy(pixelData_.get(), data, size < dataSize_ ? size : dataSize_);
				// Flush data cache so the RDP DMA sees the update
				data_cache_hit_writeback(pixelData_.get(), dataSize_);
			}
			dirty_ = true;
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

		bool IsDirty() const { return dirty_; }
		void ClearDirty() { dirty_ = false; }

		/// Translates Rhi TextureFormat to a libdragon tex_format_t.
		static tex_format_t ToNativeFormat(TextureFormat format)
		{
			switch (format) {
				case TextureFormat::RGBA8:   return FMT_RGBA32;
				case TextureFormat::RGB8:    return FMT_RGBA32;   // expand to RGBA32
				case TextureFormat::RGBA4:   return FMT_RGBA16;
				case TextureFormat::RGB5A1:  return FMT_RGBA16;
				case TextureFormat::RG8:     return FMT_IA16;
				case TextureFormat::R8:      return FMT_I8;
				case TextureFormat::R16F:    return FMT_I8;
				case TextureFormat::RG16F:   return FMT_IA16;
				case TextureFormat::RGBA16F: return FMT_RGBA32;
				case TextureFormat::R32F:    return FMT_I8;
				default:                     return FMT_RGBA16;
			}
		}

		static std::size_t BytesPerPixel(tex_format_t fmt)
		{
			switch (fmt) {
				case FMT_RGBA32: return 4;
				case FMT_RGBA16:
				case FMT_IA16:   return 2;
				case FMT_I8:
				case FMT_IA8:    return 1;
				default:         return 2;
			}
		}

		/// rdpq_tex_s32 sub-pixel coordinates for sampler filter
		static rdpq_filter_t ToNativeFilter(SamplerFilter filter)
		{
			switch (filter) {
				case SamplerFilter::Linear:
				case SamplerFilter::LinearMipmapLinear:
				case SamplerFilter::LinearMipmapNearest:
					return FILTER_BILINEAR;
				default:
					return FILTER_POINT;
			}
		}

		/// Mirror / clamp wrapping
		static rdpq_mipmap_t ToNativeMipmap(SamplerFilter filter)
		{
			switch (filter) {
				case SamplerFilter::LinearMipmapLinear:   return MIPMAP_INTERPOLATE;
				case SamplerFilter::LinearMipmapNearest:
				case SamplerFilter::NearestMipmapNearest:
				case SamplerFilter::NearestMipmapLinear:  return MIPMAP_NEAREST;
				default:                                   return MIPMAP_NONE;
			}
		}

	private:
		void FreeData()
		{
			pixelData_.reset();
			dataSize_ = 0;
		}

		std::unique_ptr<std::uint8_t[]> pixelData_;
		std::size_t     dataSize_      = 0;
		tex_format_t    nativeFormat_  = FMT_RGBA16;
		std::int32_t    width_         = 0;
		std::int32_t    height_        = 0;
		std::int32_t    mipCount_      = 0;
		TextureFormat   format_        = TextureFormat::Unknown;
		SamplerFilter   minFilter_     = SamplerFilter::Nearest;
		SamplerFilter   magFilter_     = SamplerFilter::Nearest;
		SamplerWrapping wrapS_         = SamplerWrapping::ClampToEdge;
		SamplerWrapping wrapT_         = SamplerWrapping::ClampToEdge;
		bool            dirty_         = false;
	};

}

#endif