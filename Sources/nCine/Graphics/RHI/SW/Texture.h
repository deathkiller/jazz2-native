#pragma once

#if defined(WITH_RHI_SW)

#include "../RenderTypes.h"
#include "../../../Primitives/Colorf.h"

#include <cstdint>
#include <memory>

namespace nCine::RHI
{
	/// RGBA8 surface in host memory
	class Texture
	{
	public:
		static constexpr std::int32_t MaxTextureUnitsConst = 4;

		Texture() = default;
		~Texture() = default;
		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;

		inline std::int32_t  GetWidth()   const { return width_;   }
		inline std::int32_t  GetHeight()  const { return height_;  }
		inline std::int32_t  GetMipCount() const { return mipCount_; }
		inline TextureFormat GetFormat()  const { return format_;  }
		inline SamplerFilter GetMinFilter() const { return minFilter_; }
		inline SamplerFilter GetMagFilter() const { return magFilter_; }
		inline SamplerWrapping GetWrapS() const { return wrapS_; }
		inline SamplerWrapping GetWrapT() const { return wrapT_; }

		/// Allocate storage for a mip level.
		void UploadMip(std::int32_t mipLevel, std::int32_t width, std::int32_t height, TextureFormat format, const void* data, std::size_t size);

		/// Sample the texture using nearest-neighbour filtering.
		nCine::Colorf Sample(float u, float v, std::int32_t mipLevel = 0) const;

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

		/// Returns raw RGBA8 pixel data for a given mip level (or nullptr).
		const std::uint8_t* GetPixels(std::int32_t mipLevel = 0) const;
		/// Returns mutable RGBA8 pixel data for a given mip level (or nullptr).
		std::uint8_t* GetMutablePixels(std::int32_t mipLevel = 0);
		/// Ensures mip 0 is allocated as a render target (RGBA8) at the texture's current width/height.
		void EnsureRenderTarget();

	private:
		struct MipLevel
		{
			std::int32_t width  = 0;
			std::int32_t height = 0;
			std::unique_ptr<std::uint8_t[]> data;
		};

		static constexpr std::int32_t MaxMips = 4;
		MipLevel mips_[MaxMips];
		std::int32_t  mipCount_  = 0;
		std::int32_t  width_     = 0;
		std::int32_t  height_    = 0;
		TextureFormat format_    = TextureFormat::Unknown;
		SamplerFilter minFilter_ = SamplerFilter::Nearest;
		SamplerFilter magFilter_ = SamplerFilter::Nearest;
		SamplerWrapping wrapS_   = SamplerWrapping::ClampToEdge;
		SamplerWrapping wrapT_   = SamplerWrapping::ClampToEdge;
	};
}

#endif
