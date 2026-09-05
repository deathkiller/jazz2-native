#include "GxmTexture.h"
#include "GxmDevice.h"

#include "../../../../Main.h"

#include <cstring>

namespace nCine::RHI::GXM
{
	namespace
	{
		// Copies one packed row into the store, expanding a narrower source (RGB8, R8, RG8) to the RGBA8
		// store by filling the extra channels with 255 (opaque). A same-width copy is a plain memcpy.
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

		inline SceGxmTextureFilter TranslateFilter(nCine::SamplerFilter filter)
		{
			switch (filter) {
				case nCine::SamplerFilter::Linear:
				case nCine::SamplerFilter::LinearMipmapNearest:
				case nCine::SamplerFilter::LinearMipmapLinear:
					return SCE_GXM_TEXTURE_FILTER_LINEAR;
				default:
					return SCE_GXM_TEXTURE_FILTER_POINT;
			}
		}

		inline SceGxmTextureAddrMode TranslateWrap(SamplerWrapping wrap)
		{
			switch (wrap) {
				case SamplerWrapping::Repeat: return SCE_GXM_TEXTURE_ADDR_REPEAT;
				case SamplerWrapping::MirroredRepeat: return SCE_GXM_TEXTURE_ADDR_MIRROR;
				default: return SCE_GXM_TEXTURE_ADDR_CLAMP;
			}
		}
	}

	std::uint32_t GxmTexture::_nextHandle = 1;

	GxmTexture::GxmTexture(TextureTarget target)
		: _handle(_nextHandle++), _target(target), _format(PixelFormat::Unknown), _uploadFormat(PixelFormat::Unknown),
			_width(0), _height(0), _strideBytes(0),
			_minFilter(nCine::SamplerFilter::Nearest), _magFilter(nCine::SamplerFilter::Nearest), _wrap(SamplerWrapping::ClampToEdge),
			_textureUnit(0), _isRenderTarget(false), _gpuStride(0), _gpuStrided(false), _gpuValid(false), _contentsDirty(false), _samplerDirty(true)
	{
		_swizzle[0] = SwizzleChannel::Red;
		_swizzle[1] = SwizzleChannel::Green;
		_swizzle[2] = SwizzleChannel::Blue;
		_swizzle[3] = SwizzleChannel::Alpha;
		std::memset(&_gpuTexture, 0, sizeof(_gpuTexture));
	}

	GxmTexture::~GxmTexture()
	{
		// Unbind from the device first so a later draw can't dereference this freed texture through the
		// bound-texture table, and make sure no scene the GPU has not consumed yet still samples it
		GxmDevice::UnbindTexture(this);
		ReleaseGpu();
	}

	void GxmTexture::ReleaseGpu() const
	{
		if (_gpuBlock.IsValid()) {
			// The GPU may still be reading these texels from a scene that has been submitted but not yet
			// consumed, and the memory is about to be unmapped
			GxmDevice::FinishScene();
			// Whether this block is a render target's - which is retired for reuse at the same address instead
			// of released - is decided by who owns it rather than by _isRenderTarget, which a detach has already
			// flipped by the time this runs
			GxmMemory::ReleaseSurface(_gpuBlock);
		}
		_gpuValid = false;
		_gpuStride = 0;
		_gpuStrided = false;
	}

	std::int32_t GxmTexture::BytesPerPixel(PixelFormat format)
	{
		switch (format) {
			case PixelFormat::R8: return 1;
			case PixelFormat::RG8: return 2;
			case PixelFormat::RGB565:
			case PixelFormat::RGB5A1:
			case PixelFormat::RGBA4: return 2;
			case PixelFormat::RGB8: return 3;
			case PixelFormat::RGBA8: return 4;
			case PixelFormat::RGBA16F: return 8;
			case PixelFormat::RGBA32F: return 16;
			default: return 0;
		}
	}

	bool GxmTexture::IsIdentitySwizzle() const
	{
		return (_swizzle[0] == SwizzleChannel::Red && _swizzle[1] == SwizzleChannel::Green &&
			_swizzle[2] == SwizzleChannel::Blue && _swizzle[3] == SwizzleChannel::Alpha);
	}

	void GxmTexture::Allocate(PixelFormat format, std::int32_t width, std::int32_t height)
	{
		// Keep a self-consistent 4-byte-per-texel store for the runtime formats: promote the narrower ones
		// (RGB8 render targets and the palette-index formats R8 / RG8) to an RGBA8 store, remembering the
		// original in _uploadFormat. One GPU-side format means one texture layout to get right, and it is
		// what lets the sampling swizzle be baked into the texels below.
		_uploadFormat = format;
		_format = (format == PixelFormat::RGB8 || format == PixelFormat::R8 || format == PixelFormat::RG8) ? PixelFormat::RGBA8 : format;
		_width = width;
		_height = height;
		const std::int32_t bpp = BytesPerPixel(_format);
		_strideBytes = width * bpp;
		_pixels.assign(std::size_t(_strideBytes) * std::size_t(height > 0 ? height : 0), std::uint8_t(0));
		// The size/format changed, so the GPU copy has to be rebuilt on the next bind
		ReleaseGpu();
		_contentsDirty = true;
	}

	void GxmTexture::SetRenderTarget(bool isRenderTarget)
	{
		if (isRenderTarget == _isRenderTarget) {
			return;
		}
		_isRenderTarget = isRenderTarget;
		// A colour attachment is written by the GPU, so its copy needs the write attribute; rebuild it
		ReleaseGpu();
		_contentsDirty = true;
	}

	bool GxmTexture::EnsureGpuTexture() const
	{
		if (_width <= 0 || _height <= 0 || _format != PixelFormat::RGBA8) {
			// Only the promoted RGBA8 store has a GPU-side counterpart; the float and packed formats are
			// accepted by the uploads but never sampled by this backend's shaders
			return false;
		}

		if (!_gpuBlock.IsValid()) {
			// A colour attachment gets the **tiled** layout, not a linear one. A linear texture cannot address
			// outside [0, 1] at all - the addressing mode is accepted and then simply does not happen - and every
			// render target here is sampled by a later pass that needs exactly that: the scrolling background
			// tiles its target with `SamplerWrapping::Repeat`, and the blur passes read past the edges of theirs.
			// Both produced a dark seam along the sampled edge for that reason. A tiled surface stores 32x32
			// tiles, so its memory is padded to a multiple of 32 in both directions while the *texture* keeps the
			// real size - which is why the padding costs nothing in texture coordinates, unlike the linear
			// strided layout it replaces. Sampled-only textures stay linear: they are read inside their own
			// bounds and would need their texels reordered into tiles on upload.
			const bool tiled = _isRenderTarget;
			const std::uint32_t alignedWidth = (tiled
				? ((std::uint32_t(_width) + 31u) & ~31u) : std::uint32_t(_width));
			const std::uint32_t alignedHeight = (tiled
				? ((std::uint32_t(_height) + 31u) & ~31u) : std::uint32_t(_height));
			_gpuStride = alignedWidth * 4u;
			_gpuStrided = (!tiled && (_width % 8) != 0);
			const std::uint32_t size = _gpuStride * alignedHeight;
			// A colour attachment is rendered into every frame, so it belongs in the memory the GPU writes
			// fastest; sampled-only texels are uploaded once and stay in main memory, of which there is far more.
			// A colour attachment additionally wants the address it had last time the pipeline built this target
			// (see GxmMemory::AcquireSurface()), which is why it does not go through the plain allocator
			_gpuBlock = (_isRenderTarget
				? GxmMemory::AcquireSurface("nCine:RenderTarget", _gpuStride, alignedHeight)
				: GxmMemory::Alloc("nCine:Texture", size, SCE_GXM_MEMORY_ATTRIB_READ));
			if (!_gpuBlock.IsValid()) {
				LOGE("Failed to allocate {}x{} texture ({} bytes) in GPU-visible memory", _width, _height, size);
				return false;
			}

			std::int32_t result;
			if (tiled) {
				result = sceGxmTextureInitTiled(&_gpuTexture, _gpuBlock.Base,
					SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, std::uint32_t(_width), std::uint32_t(_height), 0);
			} else if (_gpuStrided) {
				result = sceGxmTextureInitLinearStrided(&_gpuTexture, _gpuBlock.Base,
					SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, std::uint32_t(_width), std::uint32_t(_height), _gpuStride);
			} else {
				result = sceGxmTextureInitLinear(&_gpuTexture, _gpuBlock.Base,
					SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, std::uint32_t(_width), std::uint32_t(_height), 0);
			}
			if (result < 0) {
				LOGE("sceGxmTextureInit{}({}x{}) failed with 0x{:.8x}",
					tiled ? "Tiled" : (_gpuStrided ? "LinearStrided" : "Linear"),
					_width, _height, std::uint32_t(result));
				GxmMemory::ReleaseSurface(_gpuBlock);
				return false;
			}

			_gpuValid = true;
			_samplerDirty = true;
			// A render target's texels are produced by the GPU, so there is nothing to upload into it - and
			// uploading the (empty) host store would wipe a target that has already been rendered into
			_contentsDirty = !_isRenderTarget;
		}

		if (_samplerDirty) {
			ApplySamplerState();
			_samplerDirty = false;
		}
		if (_contentsDirty) {
			UploadPixels();
			_contentsDirty = false;
		}
		return _gpuValid;
	}

	void GxmTexture::ApplySamplerState() const
	{
		// A strided texture has one filter field for both directions and no mip filter at all; programming
		// either of the two rejects the call with SCE_GXM_ERROR_UNSUPPORTED, so only the shared one is set
		sceGxmTextureSetMagFilter(&_gpuTexture, TranslateFilter(_magFilter));
		if (!_gpuStrided) {
			sceGxmTextureSetMinFilter(&_gpuTexture, TranslateFilter(_minFilter));
			sceGxmTextureSetMipFilter(&_gpuTexture, SCE_GXM_TEXTURE_MIP_FILTER_DISABLED);
			// None of these textures has a mip chain, and the level count has to say so rather than be left at
			// whatever the init call defaulted to. It matters where a shader's texture coordinates jump between
			// neighbouring pixels: the textured background mirrors its layer about the horizon, so the row at
			// the fold has a derivative hundreds of times the rest of the image, and a level of detail chosen
			// from that reaches past the only level that exists. OpenGL clamps to the base level for an
			// incomplete chain, which is why that row is a black line here and not there.
			sceGxmTextureSetMipmapCount(&_gpuTexture, 1);
			sceGxmTextureSetLodBias(&_gpuTexture, 0);
		}
		// A linear layout - which is what every texture here has, strided or not - is documented to support
		// fewer addressing modes than a tiled or swizzled one, and a rejected mode leaves whatever the texture
		// was initialized with in place. That shows up as a seam wherever sampling reaches an edge: a repeating
		// background that clamps instead of wrapping draws a line at the wrap, and a blur that reads past an
		// edge picks up whatever is outside. Reported once per mode so the log says which textures wanted what.
		const SceGxmTextureAddrMode addrMode = TranslateWrap(_wrap);
		const std::int32_t uResult = sceGxmTextureSetUAddrMode(&_gpuTexture, addrMode);
		const std::int32_t vResult = sceGxmTextureSetVAddrMode(&_gpuTexture, addrMode);

		if (uResult < 0 || vResult < 0) {
			static std::uint32_t reportedModes = 0;
			const std::uint32_t modeBit = (1u << std::uint32_t(addrMode));
			if ((reportedModes & modeBit) == 0) {
				reportedModes |= modeBit;
				LOGW("A {}x{} {} texture rejected addressing mode {} with 0x{:.8x}/0x{:.8x}, so it keeps the one "
					"it was created with", _width, _height, _gpuStrided ? "linear-strided" : "linear",
					std::uint32_t(addrMode), std::uint32_t(uResult), std::uint32_t(vResult));
			}
		}
	}

	void GxmTexture::UploadPixels() const
	{
		if (!_gpuBlock.IsValid() || _pixels.empty()) {
			return;
		}

		std::uint8_t* dst = static_cast<std::uint8_t*>(_gpuBlock.Base);
		const std::uint32_t rowBytes = std::uint32_t(_width) * 4u;
		if (IsIdentitySwizzle()) {
			if (_gpuStride == rowBytes) {
				// Same stride on both sides, so the whole image is one copy
				std::memcpy(dst, _pixels.data(), std::size_t(rowBytes) * std::size_t(_height));
			} else {
				// A padded stride (a colour attachment) is filled row by row, leaving its padding untouched
				for (std::int32_t y = 0; y < _height; y++) {
					std::memcpy(dst + std::size_t(y) * _gpuStride, _pixels.data() + std::size_t(y) * rowBytes, rowBytes);
				}
			}
			return;
		}

		// sceGxm can express a channel swizzle in the texture format, but only for a fixed set of patterns -
		// not the arbitrary mapping GL's GL_TEXTURE_SWIZZLE_* allows - so a non-identity swizzle (e.g. the
		// palette-index RG8 textures set A<-Green so the shader's `src.a` reads the packed alpha byte) is
		// baked into the uploaded texels instead. Without this, `src.a` would always be 1.0 and RG8 sprites
		// (gems, pre-packed index+alpha) would lose their transparency.
		auto pick = [](SwizzleChannel channel, const std::uint8_t* texel) -> std::uint8_t {
			switch (channel) {
				case SwizzleChannel::Red: return texel[0];
				case SwizzleChannel::Green: return texel[1];
				case SwizzleChannel::Blue: return texel[2];
				case SwizzleChannel::Alpha: return texel[3];
				case SwizzleChannel::Zero: return 0;
				case SwizzleChannel::One: return 255;
				default: return texel[0];
			}
		};
		for (std::int32_t y = 0; y < _height; y++) {
			const std::uint8_t* in = _pixels.data() + std::size_t(y) * rowBytes;
			std::uint8_t* out = dst + std::size_t(y) * _gpuStride;
			for (std::int32_t x = 0; x < _width; x++, in += 4, out += 4) {
				out[0] = pick(_swizzle[0], in);
				out[1] = pick(_swizzle[1], in);
				out[2] = pick(_swizzle[2], in);
				out[3] = pick(_swizzle[3], in);
			}
		}
	}

	const SceGxmTexture* GxmTexture::GetGxmTexture() const
	{
		return (EnsureGpuTexture() ? &_gpuTexture : nullptr);
	}

	void* GxmTexture::GetSurfaceData() const
	{
		return (EnsureGpuTexture() ? _gpuBlock.Base : nullptr);
	}

	bool GxmTexture::Bind(std::uint32_t textureUnit) const
	{
		_textureUnit = textureUnit;
		GxmDevice::BindTexture(textureUnit, this);
		return true;
	}

	bool GxmTexture::Unbind() const
	{
		GxmDevice::BindTexture(_textureUnit, nullptr);
		return true;
	}

	bool GxmTexture::Unbind(std::uint32_t textureUnit)
	{
		GxmDevice::BindTexture(textureUnit, nullptr);
		return true;
	}

	void GxmTexture::TexImage2D(std::int32_t level, PixelFormat format, bool bgr, std::int32_t width, std::int32_t height, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0) {
			return;
		}

		Allocate(format, width, height);
		if (data != nullptr && !_pixels.empty()) {
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
		}
	}

	void GxmTexture::TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0 || data == nullptr || _pixels.empty()) {
			return;
		}
		if (xoffset < 0 || yoffset < 0 || xoffset + width > _width || yoffset + height > _height) {
			return;
		}

		_contentsDirty = true;
		const std::int32_t srcBpp = BytesPerPixel(format);
		const std::int32_t dstBpp = BytesPerPixel(_format);
		const std::uint8_t* src = static_cast<const std::uint8_t*>(data);
		for (std::int32_t y = 0; y < height; y++) {
			CopyExpandRow(_pixels.data() + std::size_t(yoffset + y) * _strideBytes + std::size_t(xoffset) * dstBpp,
				dstBpp, src + std::size_t(y) * std::size_t(width) * srcBpp, srcBpp, width);
		}
	}

	void GxmTexture::TexStorage2D(std::int32_t levels, PixelFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(levels);
		Allocate(format, width, height);
	}

	void GxmTexture::CompressedTexImage2D(std::int32_t level, PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t imageSize, const void* data)
	{
		static_cast<void>(level);
		static_cast<void>(format);
		static_cast<void>(width);
		static_cast<void>(height);
		static_cast<void>(imageSize);
		static_cast<void>(data);
	}

	void GxmTexture::CompressedTexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, std::int32_t imageSize, const void* data)
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

	void GxmTexture::GetTexImage(std::int32_t level, PixelFormat format, bool bgr, void* pixels)
	{
		static_cast<void>(bgr);
		if (level != 0 || pixels == nullptr || _width <= 0 || _height <= 0) {
			return;
		}

		// A render target's texels live only in its GPU copy, and the GPU has to be done writing them first
		const std::uint8_t* source = _pixels.data();
		if (_isRenderTarget && _gpuBlock.IsValid()) {
			GxmDevice::FinishScene();
			if (SceGxmContext* context = GxmDevice::GetContext()) {
				sceGxmFinish(context);
			}
			source = static_cast<const std::uint8_t*>(_gpuBlock.Base);
		}
		if (source == nullptr) {
			return;
		}

		const std::int32_t dstBpp = BytesPerPixel(format);
		const std::int32_t srcBpp = BytesPerPixel(_format);
		// A render target's rows may be padded to the stride its colour surface needed
		const std::size_t sourceStride = (source == _pixels.data()
			? std::size_t(_strideBytes) : std::size_t(_gpuStride));
		std::uint8_t* dst = static_cast<std::uint8_t*>(pixels);
		if (dstBpp == srcBpp) {
			const std::size_t rowBytes = std::size_t(_width) * std::size_t(srcBpp);
			for (std::int32_t y = 0; y < _height; y++) {
				std::memcpy(dst + std::size_t(y) * rowBytes, source + std::size_t(y) * sourceStride, rowBytes);
			}
			return;
		}
		// Narrowing readback (an RGBA8 store read back as RGB8): drop the trailing channels per texel
		const std::int32_t shared = (srcBpp < dstBpp ? srcBpp : dstBpp);
		for (std::int32_t y = 0; y < _height; y++) {
			const std::uint8_t* in = source + std::size_t(y) * sourceStride;
			std::uint8_t* out = dst + std::size_t(y) * std::size_t(_width) * std::size_t(dstBpp);
			for (std::int32_t x = 0; x < _width; x++, in += srcBpp, out += dstBpp) {
				std::int32_t c = 0;
				for (; c < shared; c++) {
					out[c] = in[c];
				}
				for (; c < dstBpp; c++) {
					out[c] = 255;
				}
			}
		}
	}

	void GxmTexture::SetMinFiltering(nCine::SamplerFilter filter)
	{
		if (_minFilter != filter) {
			_minFilter = filter;
			_samplerDirty = true;
		}
	}

	void GxmTexture::SetMagFiltering(nCine::SamplerFilter filter)
	{
		if (_magFilter != filter) {
			_magFilter = filter;
			_samplerDirty = true;
		}
	}

	void GxmTexture::SetWrap(SamplerWrapping wrap)
	{
		if (_wrap != wrap) {
			_wrap = wrap;
			_samplerDirty = true;
		}
	}

	void GxmTexture::SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a)
	{
		if (_swizzle[0] == r && _swizzle[1] == g && _swizzle[2] == b && _swizzle[3] == a) {
			return;
		}
		_swizzle[0] = r;
		_swizzle[1] = g;
		_swizzle[2] = b;
		_swizzle[3] = a;
		// The swizzle is baked into the uploaded texels, so the GPU copy is now stale
		_contentsDirty = true;
	}

	void GxmTexture::SetMaxLevel(std::int32_t maxLevel)
	{
		static_cast<void>(maxLevel);
	}

	void GxmTexture::SetUnpackAlignment(std::int32_t alignment)
	{
		static_cast<void>(alignment);
	}

	void GxmTexture::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
