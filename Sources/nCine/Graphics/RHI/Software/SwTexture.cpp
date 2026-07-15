#include "SwTexture.h"
#include "SwDevice.h"

#include <cstring>

namespace nCine::RhiSoftware
{
	namespace
	{
		// Copies one packed row into the store, expanding a narrower source (RGB8) to a wider store
		// (RGBA8) by filling the extra channel with 255 (opaque). A same-width copy is a plain memcpy.
		void CopyExpandRow(std::uint8_t* dst, std::int32_t dstBpp, const std::uint8_t* src, std::int32_t srcBpp, std::int32_t width)
		{
			if (srcBpp == dstBpp) {
				std::memcpy(dst, src, std::size_t(width) * std::size_t(dstBpp));
				return;
			}
			const std::int32_t shared = (srcBpp < dstBpp ? srcBpp : dstBpp);
			for (std::int32_t x = 0; x < width; x++) {
				std::int32_t c = 0;
				for (; c < shared; c++) {
					dst[x * dstBpp + c] = src[x * srcBpp + c];
				}
				for (; c < dstBpp; c++) {
					dst[x * dstBpp + c] = 255;
				}
			}
		}
	}

	std::uint32_t SwTexture::nextHandle_ = 1;

	SwTexture::SwTexture(TextureTarget target)
		: handle_(nextHandle_++), target_(target), format_(PixelFormat::Unknown), uploadFormat_(PixelFormat::Unknown),
			width_(0), height_(0), strideBytes_(0),
			minFilter_(nCine::SamplerFilter::Nearest), magFilter_(nCine::SamplerFilter::Nearest), wrap_(SamplerWrapping::ClampToEdge),
			textureUnit_(0), isRenderTarget_(false)
	{
		swizzle_[0] = SwizzleChannel::Red;
		swizzle_[1] = SwizzleChannel::Green;
		swizzle_[2] = SwizzleChannel::Blue;
		swizzle_[3] = SwizzleChannel::Alpha;
	}

	std::int32_t SwTexture::BytesPerPixel(PixelFormat format)
	{
		switch (format) {
			case PixelFormat::R8: return 1;
			case PixelFormat::RG8: return 2;
			case PixelFormat::RGB8: return 3;
			case PixelFormat::RGBA8: return 4;
			default: return 0;
		}
	}

	void SwTexture::Allocate(PixelFormat format, std::int32_t width, std::int32_t height)
	{
		// The CPU rasterizer composites in RGBA8 and always writes 4 bytes per texel, so a narrower store
		// would be overflowed the moment it is drawn into or the moment the engine takes its 4-byte primary
		// sample. Promote every narrower runtime format to an RGBA8 store: RGB8 (opaque render targets) and
		// the palette-index formats R8 / RG8. Uploads expand each row (see TexImage2D/TexSubImage2D, which
		// fills the extra channels with 255), so an R8 index lands as [index,255,255,255] and an RG8 one as
		// [index,alpha,255,255] - the index stays in byte 0 for the palette path, and sampling reads 4 bpp,
		// keeping the surface self-consistent whether it is later rendered into or sampled as a source. The
		// pre-promotion format is remembered in uploadFormat_ so the palette effect can still distinguish an
		// R8 index texture (alpha from the palette entry) from an RG8 one (alpha from the index texture's G).
		uploadFormat_ = format;
		format_ = (format == PixelFormat::RGB8 || format == PixelFormat::R8 || format == PixelFormat::RG8) ? PixelFormat::RGBA8 : format;
		width_ = width;
		height_ = height;
		const std::int32_t bpp = BytesPerPixel(format_);
		strideBytes_ = width * bpp;
		pixels_.assign(std::size_t(strideBytes_) * std::size_t(height > 0 ? height : 0), std::uint8_t(0));
	}

	bool SwTexture::Bind(std::uint32_t textureUnit) const
	{
		textureUnit_ = textureUnit;
		SwDevice::BindTexture(textureUnit, this);
		return true;
	}

	bool SwTexture::Unbind() const
	{
		SwDevice::BindTexture(textureUnit_, nullptr);
		return true;
	}

	bool SwTexture::Unbind(std::uint32_t textureUnit)
	{
		SwDevice::BindTexture(textureUnit, nullptr);
		return true;
	}

	void SwTexture::TexImage2D(std::int32_t level, PixelFormat format, bool bgr, std::int32_t width, std::int32_t height, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0) {
			// The fast path samples level 0 only; higher levels are accepted but not stored
			return;
		}
		Allocate(format, width, height);
		if (data != nullptr && !pixels_.empty()) {
			// `format_` may have been promoted (RGB8 -> RGBA8), so copy against the source's own bpp and
			// let CopyExpandRow widen each row when the two differ
			const std::int32_t srcBpp = BytesPerPixel(format);
			const std::int32_t dstBpp = BytesPerPixel(format_);
			if (srcBpp == dstBpp) {
				std::memcpy(pixels_.data(), data, pixels_.size());
			} else {
				const std::uint8_t* src = static_cast<const std::uint8_t*>(data);
				for (std::int32_t y = 0; y < height_; y++) {
					CopyExpandRow(pixels_.data() + std::size_t(y) * strideBytes_,
						dstBpp, src + std::size_t(y) * std::size_t(width_) * srcBpp, srcBpp, width_);
				}
			}
		}
	}

	void SwTexture::TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0 || data == nullptr || pixels_.empty()) {
			return;
		}
		const std::int32_t srcBpp = BytesPerPixel(format);
		const std::int32_t dstBpp = BytesPerPixel(format_);
		for (std::int32_t y = 0; y < height; y++) {
			const std::int32_t dstY = yoffset + y;
			if (dstY < 0 || dstY >= height_) {
				continue;
			}
			// Clamp the destination span to the texture so a sub-rect running past an edge can never write
			// past the row (and past the store on the last row)
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
			CopyExpandRow(dstRow, dstBpp, srcRow, srcBpp, copyW);
		}
	}

	void SwTexture::TexStorage2D(std::int32_t levels, PixelFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(levels);
		Allocate(format, width, height);
	}

	void SwTexture::CompressedTexImage2D(std::int32_t level, PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t imageSize, const void* data)
	{
		static_cast<void>(level);
		static_cast<void>(format);
		static_cast<void>(width);
		static_cast<void>(height);
		static_cast<void>(imageSize);
		static_cast<void>(data);
	}

	void SwTexture::CompressedTexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, std::int32_t imageSize, const void* data)
	{
		static_cast<void>(level);
		static_cast<void>(xoffset);
		static_cast<void>(yoffset);
		static_cast<void>(width);
		static_cast<void>(height);
		static_cast<void>(format);
		static_cast<void>(imageSize);
		static_cast<void>(data);
	}

	void SwTexture::GetTexImage(std::int32_t level, PixelFormat format, bool bgr, void* pixels)
	{
		static_cast<void>(level);
		static_cast<void>(format);
		static_cast<void>(bgr);
		if (pixels != nullptr && !pixels_.empty()) {
			std::memcpy(pixels, pixels_.data(), pixels_.size());
		}
	}

	void SwTexture::SetMinFiltering(nCine::SamplerFilter filter)
	{
		minFilter_ = filter;
	}

	void SwTexture::SetMagFiltering(nCine::SamplerFilter filter)
	{
		magFilter_ = filter;
	}

	void SwTexture::SetWrap(SamplerWrapping wrap)
	{
		wrap_ = wrap;
	}

	void SwTexture::SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a)
	{
		swizzle_[0] = r;
		swizzle_[1] = g;
		swizzle_[2] = b;
		swizzle_[3] = a;
	}

	void SwTexture::SetMaxLevel(std::int32_t maxLevel)
	{
		static_cast<void>(maxLevel);
	}

	void SwTexture::SetUnpackAlignment(std::int32_t alignment)
	{
		static_cast<void>(alignment);
	}

	void SwTexture::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
