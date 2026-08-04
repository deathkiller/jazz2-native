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

	std::uint32_t SwTexture::_nextHandle = 1;
	std::uint32_t SwTexture::_nextContentVersion = 0;

	SwTexture::SwTexture(TextureTarget target)
		: _handle(_nextHandle++), _contentVersion(0), _target(target), _format(PixelFormat::Unknown), _uploadFormat(PixelFormat::Unknown),
			_width(0), _height(0), _strideBytes(0), _bytesPerPixel(0),
			_minFilter(nCine::SamplerFilter::Nearest), _magFilter(nCine::SamplerFilter::Nearest), _wrap(SamplerWrapping::ClampToEdge),
			_textureUnit(0), _isRenderTarget(false)
	{
		_swizzle[0] = SwizzleChannel::Red;
		_swizzle[1] = SwizzleChannel::Green;
		_swizzle[2] = SwizzleChannel::Blue;
		_swizzle[3] = SwizzleChannel::Alpha;
	}

	SwTexture::~SwTexture()
	{
		// Clear from the device so a destroyed texture can't dangle in _boundTextures (a later deferred draw would
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
		// The requested format is remembered in _uploadFormat so the palette effect can still distinguish
		// an R8 index texture (alpha from the palette entry) from an RG8 one (alpha from the texture's G)
		// even after such a widening.
		_uploadFormat = format;
		_format = (format == PixelFormat::RGB8 || (_isRenderTarget && (format == PixelFormat::R8 || format == PixelFormat::RG8))
			? PixelFormat::RGBA8 : format);
		_width = width;
		_height = height;
		_bytesPerPixel = BytesPerPixel(_format);
		_strideBytes = width * _bytesPerPixel;
		// A deferred tile-renderer command may still reference this texture's current store (the prepared
		// command snapshots the level-0 pixel pointer at submit); drain the queue before the buffer can be
		// reallocated so no worker rasterizes from freed memory. A no-op when nothing is queued.
		if (!_pixels.empty()) {
			SwRaster::Flush();
		}
		_pixels.assign(std::size_t(_strideBytes) * std::size_t(height > 0 ? height : 0), std::uint8_t(0));
		// The store content changed; the counter is process-global so a stamp is never repeated, even by
		// a different texture object reusing this one's address
		_contentVersion = ++_nextContentVersion;
	}

	void SwTexture::SetRenderTarget(bool isRenderTarget)
	{
		_isRenderTarget = isRenderTarget;
		if (isRenderTarget && _format != PixelFormat::Unknown && _bytesPerPixel != 4) {
			// Widen a native R8/RG8 store to RGBA8 (the rasterizer composites 4 bytes per pixel). The texels
			// are discarded - a render target is always fully drawn or cleared before it is read - and
			// Allocate keeps _uploadFormat at the originally requested format and re-widens because
			// _isRenderTarget is already set.
			Allocate(_uploadFormat, _width, _height);
		}
	}

	bool SwTexture::Bind(std::uint32_t textureUnit) const
	{
		_textureUnit = textureUnit;
		SwDevice::BindTexture(textureUnit, this);
		return true;
	}

	bool SwTexture::Unbind() const
	{
		SwDevice::BindTexture(_textureUnit, nullptr);
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
		if (data != nullptr && !_pixels.empty()) {
			// `_format` may be wider than the upload (RGB8, or a render-target-widened R8/RG8), so copy
			// against the source's own bpp and let CopyExpandRow widen each row when the two differ; for
			// the native R8/RG8 stores the two match and the copy is a plain memcpy
			const std::int32_t srcBpp = BytesPerPixel(format);
			const std::int32_t dstBpp = BytesPerPixel(_format);
			if (srcBpp == dstBpp) {
				std::memcpy(_pixels.data(), data, _pixels.size());
			} else {
				const std::uint8_t* src = static_cast<const std::uint8_t*>(data);
				for (std::int32_t y = 0; y < _height; y++) {
					CopyExpandRow(_pixels.data() + std::size_t(y) * _strideBytes,
						dstBpp, src + std::size_t(y) * std::size_t(_width) * srcBpp, srcBpp, _width);
				}
			}
			_contentVersion = ++_nextContentVersion;
		}
	}

	void SwTexture::TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0 || data == nullptr || _pixels.empty()) {
			return;
		}
		const std::int32_t srcBpp = BytesPerPixel(format);
		const std::int32_t dstBpp = BytesPerPixel(_format);
		for (std::int32_t y = 0; y < height; y++) {
			const std::int32_t dstY = yoffset + y;
			if (dstY < 0 || dstY >= _height) {
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
			if (dstX + copyW > _width) {
				copyW = _width - dstX;
			}
			if (copyW <= 0) {
				continue;
			}
			const std::uint8_t* srcRow = static_cast<const std::uint8_t*>(data) + std::size_t(y) * std::size_t(width) * srcBpp + std::size_t(srcX0) * srcBpp;
			std::uint8_t* dstRow = _pixels.data() + std::size_t(dstY) * _strideBytes + std::size_t(dstX) * dstBpp;
			CopyExpandRow(dstRow, dstBpp, srcRow, srcBpp, copyW);
		}
		_contentVersion = ++_nextContentVersion;
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
		if (pixels == nullptr || _pixels.empty()) {
			return;
		}
		const std::int32_t dstBpp = BytesPerPixel(format);
		if (dstBpp <= 0 || dstBpp == _bytesPerPixel) {
			// The store already matches the requested layout (the common case - native R8/RG8 reads back
			// exactly the bytes that were uploaded, which the promoted store never did)
			std::memcpy(pixels, _pixels.data(), _pixels.size());
			return;
		}
		// A widened store read back at the original format (RGB8 upload, or a render-target-widened
		// R8/RG8): narrow each texel to the requested channel count (or widen with 255, matching the
		// samplers' expansion, if the caller asks for more channels than are stored)
		const std::size_t count = std::size_t(_width) * std::size_t(_height > 0 ? _height : 0);
		const std::uint8_t* src = _pixels.data();
		std::uint8_t* dst = static_cast<std::uint8_t*>(pixels);
		const std::int32_t shared = (_bytesPerPixel < dstBpp ? _bytesPerPixel : dstBpp);
		for (std::size_t i = 0; i < count; i++) {
			std::int32_t c = 0;
			for (; c < shared; c++) {
				dst[c] = src[c];
			}
			for (; c < dstBpp; c++) {
				dst[c] = 255;
			}
			src += _bytesPerPixel;
			dst += dstBpp;
		}
	}

	void SwTexture::SetMinFiltering(nCine::SamplerFilter filter)
	{
		_minFilter = filter;
	}

	void SwTexture::SetMagFiltering(nCine::SamplerFilter filter)
	{
		_magFilter = filter;
	}

	void SwTexture::SetWrap(SamplerWrapping wrap)
	{
		_wrap = wrap;
	}

	void SwTexture::SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a)
	{
		_swizzle[0] = r;
		_swizzle[1] = g;
		_swizzle[2] = b;
		_swizzle[3] = a;
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
