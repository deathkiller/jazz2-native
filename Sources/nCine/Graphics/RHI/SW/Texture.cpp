#if defined(WITH_RHI_SW)

#include "Texture.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nCine::RHI
{
	void Texture::UploadMip(std::int32_t mipLevel, std::int32_t width, std::int32_t height, TextureFormat format,
	                        const void* data, std::size_t size)
	{
		if (mipLevel < 0 || mipLevel >= MaxMips) return;

		mips_[mipLevel].width  = width;
		mips_[mipLevel].height = height;

		std::size_t byteSize = size;
		// Only RGBA8 is directly stored; other formats converted at upload
		if (format == TextureFormat::RGBA8) {
			byteSize = static_cast<std::size_t>(width) * height * 4;
		} else if (format == TextureFormat::RGB8) {
			byteSize = static_cast<std::size_t>(width) * height * 4;
		}

		mips_[mipLevel].data = std::make_unique<std::uint8_t[]>(byteSize);

		if (data != nullptr) {
			if (format == TextureFormat::RGBA8) {
				std::memcpy(mips_[mipLevel].data.get(), data, byteSize);
			} else if (format == TextureFormat::RGB8) {
				// Convert RGB8 → RGBA8
				const std::uint8_t* src  = static_cast<const std::uint8_t*>(data);
				std::uint8_t*       dst  = mips_[mipLevel].data.get();
				std::int32_t pixels = width * height;
				for (std::int32_t i = 0; i < pixels; ++i) {
					dst[0] = src[0];
					dst[1] = src[1];
					dst[2] = src[2];
					dst[3] = 255;
					src += 3;
					dst += 4;
				}
			} else {
				// Generic fallback: copy raw bytes
				std::memcpy(mips_[mipLevel].data.get(), data, size);
			}
		}

		if (mipLevel == 0) {
			width_    = width;
			height_   = height;
			format_   = format;
		}
		if (mipLevel + 1 > mipCount_) {
			mipCount_ = mipLevel + 1;
		}
	}

	const std::uint8_t* Texture::GetPixels(std::int32_t mipLevel) const
	{
		if (mipLevel < 0 || mipLevel >= mipCount_) return nullptr;
		return mips_[mipLevel].data.get();
	}

	std::uint8_t* Texture::GetMutablePixels(std::int32_t mipLevel)
	{
		if (mipLevel < 0 || mipLevel >= mipCount_) return nullptr;
		return mips_[mipLevel].data.get();
	}

	void Texture::EnsureRenderTarget()
	{
		if (width_ <= 0 || height_ <= 0) return;
		MipLevel& m = mips_[0];
		if (m.data == nullptr || m.width != width_ || m.height != height_) {
			m.width  = width_;
			m.height = height_;
			m.data   = std::make_unique<std::uint8_t[]>(static_cast<std::size_t>(width_) * height_ * 4);
			if (mipCount_ < 1) mipCount_ = 1;
		}
		isRenderTarget_ = true;
	}

	Colorf Texture::Sample(float u, float v, std::int32_t mipLevel) const
	{
		if (mipLevel < 0 || mipLevel >= mipCount_) {
			return Colorf(1, 1, 1, 1);
		}

		const MipLevel& mip = mips_[mipLevel];
		if (mip.data == nullptr || mip.width == 0 || mip.height == 0) {
			return Colorf(1, 1, 1, 1);
		}

		// Apply wrapping
		auto wrapCoord = [](float t, SamplerWrapping mode) -> float {
			switch (mode) {
				case SamplerWrapping::Repeat:
					t -= std::floor(t);
					return t;
				case SamplerWrapping::MirroredRepeat: {
					float f = std::floor(t);
					t -= f;
					if (static_cast<std::int32_t>(f) & 1) t = 1.0f - t;
					return t;
				}
				case SamplerWrapping::ClampToEdge:
				default:
					return std::fmax(0.0f, std::fmin(1.0f, t));
			}
		};

		u = wrapCoord(u, wrapS_);
		v = wrapCoord(v, wrapT_);

		const std::int32_t px = static_cast<std::int32_t>(u * (mip.width  - 1) + 0.5f);
		const std::int32_t py = static_cast<std::int32_t>(v * (mip.height - 1) + 0.5f);
		const std::int32_t clampedX = std::max(0, std::min(mip.width  - 1, px));
		const std::int32_t clampedY = std::max(0, std::min(mip.height - 1, py));

		const std::uint8_t* pixel = mip.data.get() + (clampedY * mip.width + clampedX) * 4;
		return Colorf(pixel[0] / 255.0f, pixel[1] / 255.0f, pixel[2] / 255.0f, pixel[3] / 255.0f);
	}
}

#endif
