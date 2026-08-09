#if defined(WITH_RHI_RSX)

#include "RsxTexture.h"
#include "RsxDevice.h"

#include "../../../../Main.h"

#include <cstring>

#include <rsx/rsx.h>
#include <rsx/commands.h>

namespace nCine::RHI::RSX
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

		/**
			@brief Translates a minification filter, which is the only one that may mention mip levels

			The store is single-level, so a mipmapped mode is reduced to its base filter rather than being
			refused: the texture simply has nothing to minify into, and the hardware would sample level 0
			regardless.
		*/
		inline std::uint8_t TranslateMinFilter(nCine::SamplerFilter filter)
		{
			switch (filter) {
				case nCine::SamplerFilter::Linear:
				case nCine::SamplerFilter::LinearMipmapNearest:
				case nCine::SamplerFilter::LinearMipmapLinear:
					return GCM_TEXTURE_LINEAR;
				default:
					return GCM_TEXTURE_NEAREST;
			}
		}

		inline std::uint8_t TranslateMagFilter(nCine::SamplerFilter filter)
		{
			return (filter == nCine::SamplerFilter::Linear ? GCM_TEXTURE_LINEAR : GCM_TEXTURE_NEAREST);
		}

		inline std::uint8_t TranslateWrap(SamplerWrapping wrap)
		{
			switch (wrap) {
				case SamplerWrapping::Repeat: return GCM_TEXTURE_REPEAT;
				case SamplerWrapping::MirroredRepeat: return GCM_TEXTURE_MIRRORED_REPEAT;
				default: return GCM_TEXTURE_CLAMP_TO_EDGE;
			}
		}

		/**
			@brief Which hardware channel of the A8R8G8B8 word carries a logical RGBA8 channel

			The engine's store holds bytes in R, G, B, A order. Read as one big-endian word that is
			`0xRRGGBBAA`, and the hardware reads a word as A8R8G8B8 - so its A field holds the logical red,
			its R field the logical green, and so on round the cycle. Sampling therefore needs the rotation
			this table expresses, which @ref RsxTexture::BuildRemap() folds into the swizzle for free.
		*/
		inline std::uint32_t HardwareChannelFor(SwizzleChannel channel)
		{
			switch (channel) {
				case SwizzleChannel::Red: return GCM_TEXTURE_REMAP_COLOR_A;
				case SwizzleChannel::Green: return GCM_TEXTURE_REMAP_COLOR_R;
				case SwizzleChannel::Blue: return GCM_TEXTURE_REMAP_COLOR_G;
				case SwizzleChannel::Alpha: return GCM_TEXTURE_REMAP_COLOR_B;
				default: return GCM_TEXTURE_REMAP_COLOR_A;
			}
		}

		/** @brief Row-stride granularity of the GPU copy (see @ref RsxTexture) */
		constexpr std::uint32_t TexturePitchAlignment = 64;
	}

	std::uint32_t RsxTexture::_nextHandle = 1;

	RsxTexture::RsxTexture(TextureTarget target)
		: _handle(_nextHandle++), _target(target), _format(PixelFormat::Unknown), _uploadFormat(PixelFormat::Unknown),
			_width(0), _height(0), _strideBytes(0),
			_minFilter(nCine::SamplerFilter::Nearest), _magFilter(nCine::SamplerFilter::Nearest), _wrap(SamplerWrapping::ClampToEdge),
			_textureUnit(0), _isRenderTarget(false), _gpuStride(0), _gpuValid(false), _contentsDirty(false)
	{
		_swizzle[0] = SwizzleChannel::Red;
		_swizzle[1] = SwizzleChannel::Green;
		_swizzle[2] = SwizzleChannel::Blue;
		_swizzle[3] = SwizzleChannel::Alpha;
		std::memset(&_gpuTexture, 0, sizeof(_gpuTexture));
	}

	RsxTexture::~RsxTexture()
	{
		// The device tracks bound textures by pointer, so it has to forget this one before the memory goes
		RsxDevice::UnbindTexture(this);
		ReleaseGpu();
	}

	void RsxTexture::ReleaseGpu() const
	{
		if (_gpuBlock.IsValid()) {
			// The GPU may still be reading these texels from a frame that has not finished, so the block is
			// handed to the device's retirement list rather than freed under the hardware
			RsxDevice::RetireBlock(_gpuBlock);
		}
		_gpuValid = false;
		_gpuStride = 0;
	}

	std::int32_t RsxTexture::BytesPerPixel(PixelFormat format)
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

	std::uint32_t RsxTexture::BuildRemap() const
	{
		// Each output channel independently picks a hardware source channel, or a constant. The engine's
		// swizzle says which LOGICAL channel feeds each output; HardwareChannelFor() turns that into the
		// hardware channel the RGBA8-as-A8R8G8B8 reinterpretation put it in (see the class documentation).
		const auto typeOf = [](SwizzleChannel channel) -> std::uint32_t {
			switch (channel) {
				case SwizzleChannel::Zero: return GCM_TEXTURE_REMAP_TYPE_ZERO;
				case SwizzleChannel::One: return GCM_TEXTURE_REMAP_TYPE_ONE;
				default: return GCM_TEXTURE_REMAP_TYPE_REMAP;
			}
		};

		return ((typeOf(_swizzle[3]) << GCM_TEXTURE_REMAP_TYPE_A_SHIFT) |
				(typeOf(_swizzle[0]) << GCM_TEXTURE_REMAP_TYPE_R_SHIFT) |
				(typeOf(_swizzle[1]) << GCM_TEXTURE_REMAP_TYPE_G_SHIFT) |
				(typeOf(_swizzle[2]) << GCM_TEXTURE_REMAP_TYPE_B_SHIFT) |
				(HardwareChannelFor(_swizzle[3]) << GCM_TEXTURE_REMAP_COLOR_A_SHIFT) |
				(HardwareChannelFor(_swizzle[0]) << GCM_TEXTURE_REMAP_COLOR_R_SHIFT) |
				(HardwareChannelFor(_swizzle[1]) << GCM_TEXTURE_REMAP_COLOR_G_SHIFT) |
				(HardwareChannelFor(_swizzle[2]) << GCM_TEXTURE_REMAP_COLOR_B_SHIFT));
	}

	void RsxTexture::Allocate(PixelFormat format, std::int32_t width, std::int32_t height)
	{
		// Keep a self-consistent 4-byte-per-texel store for the runtime formats: promote the narrower ones
		// (RGB8 render targets and the palette-index formats R8 / RG8) to an RGBA8 store, remembering the
		// original in _uploadFormat. One GPU-side format means one texture layout to get right.
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

	void RsxTexture::SetRenderTarget(bool isRenderTarget)
	{
		if (isRenderTarget == _isRenderTarget) {
			return;
		}
		_isRenderTarget = isRenderTarget;
		// Nothing about the allocation changes - an RSX surface and an RSX texture are the same memory read
		// two ways - but a target's texels stop coming from the host store, so a pending upload would
		// overwrite what the GPU drew. Rebuilding makes the transition explicit either way round.
		ReleaseGpu();
		_contentsDirty = !isRenderTarget;
	}

	bool RsxTexture::EnsureGpuTexture() const
	{
		if (_width <= 0 || _height <= 0 || _format != PixelFormat::RGBA8) {
			// Only the promoted RGBA8 store has a GPU-side counterpart; the float and packed formats are
			// accepted by the uploads but never sampled by this backend's shaders
			return false;
		}

		if (!_gpuBlock.IsValid()) {
			// Local memory, always. A texture is written once by the PPE and then sampled repeatedly by the
			// GPU, which is exactly the access pattern GDDR3's uncached-write / fast-read asymmetry suits
			// (see RsxVram). That holds for a render target too - there the PPE does not write it at all.
			const std::uint32_t pitch = (std::uint32_t(_width) * 4u + TexturePitchAlignment - 1) & ~(TexturePitchAlignment - 1);
			_gpuBlock = RsxVram::Alloc(pitch * std::uint32_t(_height), TexturePitchAlignment, RsxVram::Location::Local);
			if (!_gpuBlock.IsValid()) {
				LOGE("Cannot allocate a {}x{} texture in local memory", _width, _height);
				return false;
			}
			_gpuStride = pitch;
			_contentsDirty = !_isRenderTarget;

			std::memset(&_gpuTexture, 0, sizeof(_gpuTexture));
			// LIN: an explicit pitch, rather than the swizzled layout that would derive one from a
			// power-of-two width. NRM would normalize the coordinates the other way round from what the
			// engine's shaders expect, so it is deliberately absent - the RSX defaults to normalized.
			_gpuTexture.format = (GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_LIN);
			_gpuTexture.mipmap = 1;
			_gpuTexture.dimension = GCM_TEXTURE_DIMS_2D;
			_gpuTexture.cubemap = GCM_FALSE;
			_gpuTexture.width = std::uint16_t(_width);
			_gpuTexture.height = std::uint16_t(_height);
			_gpuTexture.depth = 1;
			_gpuTexture.location = _gpuBlock.GetGcmLocation();
			_gpuTexture.pitch = pitch;
			_gpuTexture.offset = _gpuBlock.Offset;
			_gpuValid = true;
		}

		// The remap is recomputed unconditionally: it is one word, and it depends only on the swizzle, so
		// there is nothing to invalidate and nothing to get out of step
		_gpuTexture.remap = BuildRemap();

		if (_contentsDirty) {
			UploadPixels();
			_contentsDirty = false;
		}
		return _gpuValid;
	}

	void RsxTexture::UploadPixels() const
	{
		if (!_gpuBlock.IsValid() || _pixels.empty()) {
			return;
		}

		// The texels go across unchanged - no swizzle is baked in and no byte order is fixed up, because the
		// sampler's remap does both (see the class documentation). Only the row padding differs between the
		// tightly packed host store and the aligned GPU pitch.
		std::uint8_t* dst = static_cast<std::uint8_t*>(_gpuBlock.Base);
		const std::uint32_t rowBytes = std::uint32_t(_width) * 4u;
		if (_gpuStride == rowBytes) {
			std::memcpy(dst, _pixels.data(), std::size_t(rowBytes) * std::size_t(_height));
		} else {
			for (std::int32_t y = 0; y < _height; y++) {
				std::memcpy(dst + std::size_t(y) * _gpuStride, _pixels.data() + std::size_t(y) * rowBytes, rowBytes);
			}
		}
	}

	void RsxTexture::ApplySamplerState(std::uint32_t textureUnit) const
	{
		gcmContextData* context = RsxDevice::GetContext();
		if (context == nullptr) {
			return;
		}

		const std::uint8_t index = std::uint8_t(textureUnit);
		// Enable the unit with a single mip level (min and max LOD both 0, in the 8.8 fixed point the
		// command takes) and no anisotropy - the store has one level, so anything else would be a lie
		rsxTextureControl(context, index, GCM_TRUE, 0 << 8, 0 << 8, GCM_TEXTURE_MAX_ANISO_1);
		rsxTextureFilter(context, index, 0, TranslateMinFilter(_minFilter), TranslateMagFilter(_magFilter),
			GCM_TEXTURE_CONVOLUTION_QUINCUNX);

		// Unlike the sceGxm backend, no addressing mode has to be given up here: the RSX's linear layout
		// accepts repeat and mirrored-repeat as readily as clamp, so the scrolling background's Repeat and
		// the blur passes reading past an edge both work on the layout every texture already has.
		const std::uint8_t wrap = TranslateWrap(_wrap);
		rsxTextureWrapMode(context, index, wrap, wrap, GCM_TEXTURE_CLAMP_TO_EDGE, 0, GCM_TEXTURE_ZFUNC_LESS, 0);
	}

	const gcmTexture* RsxTexture::GetGcmTexture() const
	{
		return (EnsureGpuTexture() ? &_gpuTexture : nullptr);
	}

	void* RsxTexture::GetSurfaceData() const
	{
		return (EnsureGpuTexture() ? _gpuBlock.Base : nullptr);
	}

	bool RsxTexture::Bind(std::uint32_t textureUnit) const
	{
		_textureUnit = textureUnit;
		RsxDevice::BindTexture(textureUnit, this);
		return true;
	}

	bool RsxTexture::Unbind() const
	{
		RsxDevice::BindTexture(_textureUnit, nullptr);
		return true;
	}

	bool RsxTexture::Unbind(std::uint32_t textureUnit)
	{
		RsxDevice::BindTexture(textureUnit, nullptr);
		return true;
	}

	void RsxTexture::TexImage2D(std::int32_t level, PixelFormat format, bool bgr, std::int32_t width, std::int32_t height, const void* data)
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

	void RsxTexture::TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data)
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

	void RsxTexture::TexStorage2D(std::int32_t levels, PixelFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(levels);
		Allocate(format, width, height);
	}

	void RsxTexture::CompressedTexImage2D(std::int32_t level, PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t imageSize, const void* data)
	{
		// The RSX does have DXT sampling, but the engine's console content is delivered uncompressed and
		// nothing in the pipeline reaches this path; accepted as a no-op rather than half-implemented
		static_cast<void>(level);
		static_cast<void>(format);
		static_cast<void>(width);
		static_cast<void>(height);
		static_cast<void>(imageSize);
		static_cast<void>(data);
	}

	void RsxTexture::CompressedTexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, std::int32_t imageSize, const void* data)
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

	void RsxTexture::GetTexImage(std::int32_t level, PixelFormat format, bool bgr, void* pixels)
	{
		static_cast<void>(bgr);
		if (level != 0 || pixels == nullptr || _width <= 0 || _height <= 0) {
			return;
		}

		// A render target's texels live only in its GPU copy, and the GPU has to be done writing them first.
		// This read comes out of local memory, which the PPE reaches uncached across the bus - it is by far
		// the slowest thing this backend can do, and the reason nothing on a frame path calls it.
		const std::uint8_t* source = _pixels.data();
		std::size_t sourceStride = std::size_t(_strideBytes);
		if (_isRenderTarget && _gpuBlock.IsValid()) {
			RsxDevice::Finish();
			source = static_cast<const std::uint8_t*>(_gpuBlock.Base);
			sourceStride = std::size_t(_gpuStride);
		}
		if (source == nullptr) {
			return;
		}

		const std::int32_t dstBpp = BytesPerPixel(format);
		const std::int32_t srcBpp = BytesPerPixel(_format);
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

	void* RsxTexture::MapStreamingTexels(std::int32_t& strideBytes)
	{
		// A render target's texels belong to the GPU, and only the promoted RGBA8 store has a GPU copy at all
		if (_isRenderTarget || _format != PixelFormat::RGBA8) {
			return nullptr;
		}
		if (!EnsureGpuTexture()) {
			return nullptr;
		}

		// The caller overwrites the whole surface every frame, so the host store is now stale by definition;
		// clearing the dirty flag stops the next bind from copying it back over what was just written
		_contentsDirty = false;

		strideBytes = std::int32_t(_gpuStride);
		return _gpuBlock.Base;
	}

	void RsxTexture::SetMinFiltering(nCine::SamplerFilter filter)
	{
		_minFilter = filter;
	}

	void RsxTexture::SetMagFiltering(nCine::SamplerFilter filter)
	{
		_magFilter = filter;
	}

	void RsxTexture::SetWrap(SamplerWrapping wrap)
	{
		_wrap = wrap;
	}

	void RsxTexture::SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a)
	{
		// Only the remap word changes; the texels stay as they were uploaded, which is what makes this cheap
		// enough for the pipeline to set per material rather than per texture creation
		_swizzle[0] = r;
		_swizzle[1] = g;
		_swizzle[2] = b;
		_swizzle[3] = a;
	}

	void RsxTexture::SetMaxLevel(std::int32_t maxLevel)
	{
		static_cast<void>(maxLevel);
	}

	void RsxTexture::SetUnpackAlignment(std::int32_t alignment)
	{
		static_cast<void>(alignment);
	}

	void RsxTexture::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}

#endif
