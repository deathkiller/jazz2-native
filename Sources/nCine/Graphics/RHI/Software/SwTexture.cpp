#include "SwTexture.h"
#include "SwDevice.h"
#include "SwRaster.h"

#include <cstring>

namespace nCine::RHI::Software
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
	std::uint32_t SwTexture::nextContentVersion_ = 0;

	SwTexture::SwTexture(TextureTarget target)
		: handle_(nextHandle_++), contentVersion_(0), target_(target), format_(PixelFormat::Unknown), uploadFormat_(PixelFormat::Unknown),
			width_(0), height_(0), strideBytes_(0), bytesPerPixel_(0),
			minFilter_(nCine::SamplerFilter::Nearest), magFilter_(nCine::SamplerFilter::Nearest), wrap_(SamplerWrapping::ClampToEdge),
			textureUnit_(0), isRenderTarget_(false)
	{
		swizzle_[0] = SwizzleChannel::Red;
		swizzle_[1] = SwizzleChannel::Green;
		swizzle_[2] = SwizzleChannel::Blue;
		swizzle_[3] = SwizzleChannel::Alpha;
	}

	SwTexture::~SwTexture()
	{
		// Clear from the device so a destroyed texture can't dangle in boundTextures_ (a later deferred draw would
		// dereference freed memory in Dispatch)
		SwDevice::UnbindTexture(this);
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
		// R8 / RG8 index textures are stored NATIVELY (1 / 2 bytes per texel): the samplers expand each
		// fetched texel to the 4-byte RGBA working form on the fly, filling the missing channels with 255 -
		// so an R8 index samples as [index,255,255,255] and an RG8 one as [index,alpha,255,255], the exact
		// bytes the former promoted-to-RGBA8 store held. That makes indexed sprite atlases and tilesets
		// (the bulk of this game's texel data) 4x / 2x smaller in memory and in gather bandwidth. Only two
		// cases still widen to an RGBA8 store: RGB8 (3-byte texels are unaligned, and it is the render-
		// target format of the shader path), and any texture attached as a render target (the rasterizer
		// composites 4 bytes per pixel - see SetRenderTarget, which re-widens an already-native store).
		// The requested format is remembered in uploadFormat_ so the palette effect can still distinguish
		// an R8 index texture (alpha from the palette entry) from an RG8 one (alpha from the texture's G)
		// even after such a widening.
		uploadFormat_ = format;
		format_ = (format == PixelFormat::RGB8 || (isRenderTarget_ && (format == PixelFormat::R8 || format == PixelFormat::RG8))
			? PixelFormat::RGBA8 : format);
		width_ = width;
		height_ = height;
		bytesPerPixel_ = BytesPerPixel(format_);
		strideBytes_ = width * bytesPerPixel_;
		// A deferred tile-renderer command may still reference this texture's current store (the prepared
		// command snapshots the level-0 pixel pointer at submit); drain the queue before the buffer can be
		// reallocated so no worker rasterizes from freed memory. A no-op when nothing is queued.
		if (!pixels_.empty()) {
			SwRaster::Flush();
		}
		pixels_.assign(std::size_t(strideBytes_) * std::size_t(height > 0 ? height : 0), std::uint8_t(0));
		// The store content changed; the counter is process-global so a stamp is never repeated, even by
		// a different texture object reusing this one's address
		contentVersion_ = ++nextContentVersion_;
	}

	void SwTexture::SetRenderTarget(bool isRenderTarget)
	{
		isRenderTarget_ = isRenderTarget;
		if (isRenderTarget && format_ != PixelFormat::Unknown && bytesPerPixel_ != 4) {
			// Widen a native R8/RG8 store to RGBA8 (the rasterizer composites 4 bytes per pixel). The texels
			// are discarded - a render target is always fully drawn or cleared before it is read - and
			// Allocate keeps uploadFormat_ at the originally requested format and re-widens because
			// isRenderTarget_ is already set.
			Allocate(uploadFormat_, width_, height_);
		}
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
			// `format_` may be wider than the upload (RGB8, or a render-target-widened R8/RG8), so copy
			// against the source's own bpp and let CopyExpandRow widen each row when the two differ; for
			// the native R8/RG8 stores the two match and the copy is a plain memcpy
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
			contentVersion_ = ++nextContentVersion_;
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
		contentVersion_ = ++nextContentVersion_;
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
		static_cast<void>(bgr);
		if (pixels == nullptr || pixels_.empty()) {
			return;
		}
		const std::int32_t dstBpp = BytesPerPixel(format);
		if (dstBpp <= 0 || dstBpp == bytesPerPixel_) {
			// The store already matches the requested layout (the common case - native R8/RG8 reads back
			// exactly the bytes that were uploaded, which the promoted store never did)
			std::memcpy(pixels, pixels_.data(), pixels_.size());
			return;
		}
		// A widened store read back at the original format (RGB8 upload, or a render-target-widened
		// R8/RG8): narrow each texel to the requested channel count (or widen with 255, matching the
		// samplers' expansion, if the caller asks for more channels than are stored)
		const std::size_t count = std::size_t(width_) * std::size_t(height_ > 0 ? height_ : 0);
		const std::uint8_t* src = pixels_.data();
		std::uint8_t* dst = static_cast<std::uint8_t*>(pixels);
		const std::int32_t shared = (bytesPerPixel_ < dstBpp ? bytesPerPixel_ : dstBpp);
		for (std::size_t i = 0; i < count; i++) {
			std::int32_t c = 0;
			for (; c < shared; c++) {
				dst[c] = src[c];
			}
			for (; c < dstBpp; c++) {
				dst[c] = 255;
			}
			src += bytesPerPixel_;
			dst += dstBpp;
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
