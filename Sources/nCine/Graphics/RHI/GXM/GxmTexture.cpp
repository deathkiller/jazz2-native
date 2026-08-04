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

	std::uint32_t GxmTexture::nextHandle_ = 1;

	GxmTexture::GxmTexture(TextureTarget target)
		: handle_(nextHandle_++), target_(target), format_(PixelFormat::Unknown), uploadFormat_(PixelFormat::Unknown),
			width_(0), height_(0), strideBytes_(0),
			minFilter_(nCine::SamplerFilter::Nearest), magFilter_(nCine::SamplerFilter::Nearest), wrap_(SamplerWrapping::ClampToEdge),
			textureUnit_(0), isRenderTarget_(false), gpuStride_(0), gpuStrided_(false), gpuValid_(false), contentsDirty_(false), samplerDirty_(true)
	{
		swizzle_[0] = SwizzleChannel::Red;
		swizzle_[1] = SwizzleChannel::Green;
		swizzle_[2] = SwizzleChannel::Blue;
		swizzle_[3] = SwizzleChannel::Alpha;
		std::memset(&gpuTexture_, 0, sizeof(gpuTexture_));
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
		if (gpuBlock_.IsValid()) {
			// The GPU may still be reading these texels from a scene that has been submitted but not yet
			// consumed, and the memory is about to be unmapped
			GxmDevice::FinishScene();
			// Whether this block is a render target's - which is retired for reuse at the same address instead
			// of released - is decided by who owns it rather than by isRenderTarget_, which a detach has already
			// flipped by the time this runs
			GxmMemory::ReleaseSurface(gpuBlock_);
		}
		gpuValid_ = false;
		gpuStride_ = 0;
		gpuStrided_ = false;
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
		return (swizzle_[0] == SwizzleChannel::Red && swizzle_[1] == SwizzleChannel::Green &&
			swizzle_[2] == SwizzleChannel::Blue && swizzle_[3] == SwizzleChannel::Alpha);
	}

	void GxmTexture::Allocate(PixelFormat format, std::int32_t width, std::int32_t height)
	{
		// Keep a self-consistent 4-byte-per-texel store for the runtime formats: promote the narrower ones
		// (RGB8 render targets and the palette-index formats R8 / RG8) to an RGBA8 store, remembering the
		// original in uploadFormat_. One GPU-side format means one texture layout to get right, and it is
		// what lets the sampling swizzle be baked into the texels below.
		uploadFormat_ = format;
		format_ = (format == PixelFormat::RGB8 || format == PixelFormat::R8 || format == PixelFormat::RG8) ? PixelFormat::RGBA8 : format;
		width_ = width;
		height_ = height;
		const std::int32_t bpp = BytesPerPixel(format_);
		strideBytes_ = width * bpp;
		pixels_.assign(std::size_t(strideBytes_) * std::size_t(height > 0 ? height : 0), std::uint8_t(0));
		// The size/format changed, so the GPU copy has to be rebuilt on the next bind
		ReleaseGpu();
		contentsDirty_ = true;
	}

	void GxmTexture::SetRenderTarget(bool isRenderTarget)
	{
		if (isRenderTarget == isRenderTarget_) {
			return;
		}
		isRenderTarget_ = isRenderTarget;
		// A colour attachment is written by the GPU, so its copy needs the write attribute; rebuild it
		ReleaseGpu();
		contentsDirty_ = true;
	}

	bool GxmTexture::EnsureGpuTexture() const
	{
		if (width_ <= 0 || height_ <= 0 || format_ != PixelFormat::RGBA8) {
			// Only the promoted RGBA8 store has a GPU-side counterpart; the float and packed formats are
			// accepted by the uploads but never sampled by this backend's shaders
			return false;
		}

		if (!gpuBlock_.IsValid()) {
			// A colour attachment gets the **tiled** layout, not a linear one. A linear texture cannot address
			// outside [0, 1] at all - the addressing mode is accepted and then simply does not happen - and every
			// render target here is sampled by a later pass that needs exactly that: the scrolling background
			// tiles its target with `SamplerWrapping::Repeat`, and the blur passes read past the edges of theirs.
			// Both produced a dark seam along the sampled edge for that reason. A tiled surface stores 32x32
			// tiles, so its memory is padded to a multiple of 32 in both directions while the *texture* keeps the
			// real size - which is why the padding costs nothing in texture coordinates, unlike the linear
			// strided layout it replaces. Sampled-only textures stay linear: they are read inside their own
			// bounds and would need their texels reordered into tiles on upload.
			const bool tiled = isRenderTarget_;
			const std::uint32_t alignedWidth = (tiled
				? ((std::uint32_t(width_) + 31u) & ~31u) : std::uint32_t(width_));
			const std::uint32_t alignedHeight = (tiled
				? ((std::uint32_t(height_) + 31u) & ~31u) : std::uint32_t(height_));
			gpuStride_ = alignedWidth * 4u;
			gpuStrided_ = (!tiled && (width_ % 8) != 0);
			const std::uint32_t size = gpuStride_ * alignedHeight;
			// A colour attachment is rendered into every frame, so it belongs in the memory the GPU writes
			// fastest; sampled-only texels are uploaded once and stay in main memory, of which there is far more.
			// A colour attachment additionally wants the address it had last time the pipeline built this target
			// (see GxmMemory::AcquireSurface()), which is why it does not go through the plain allocator
			gpuBlock_ = (isRenderTarget_
				? GxmMemory::AcquireSurface("Jazz2:RenderTarget", gpuStride_, alignedHeight)
				: GxmMemory::Alloc("Jazz2:Texture", size, SCE_GXM_MEMORY_ATTRIB_READ));
			if (!gpuBlock_.IsValid()) {
				LOGE("Failed to allocate {}x{} texture ({} bytes) in GPU-visible memory", width_, height_, size);
				return false;
			}

			std::int32_t result;
			if (tiled) {
				result = sceGxmTextureInitTiled(&gpuTexture_, gpuBlock_.Base,
					SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, std::uint32_t(width_), std::uint32_t(height_), 0);
			} else if (gpuStrided_) {
				result = sceGxmTextureInitLinearStrided(&gpuTexture_, gpuBlock_.Base,
					SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, std::uint32_t(width_), std::uint32_t(height_), gpuStride_);
			} else {
				result = sceGxmTextureInitLinear(&gpuTexture_, gpuBlock_.Base,
					SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, std::uint32_t(width_), std::uint32_t(height_), 0);
			}
			if (result < 0) {
				LOGE("sceGxmTextureInit{}({}x{}) failed with 0x{:.8x}",
					tiled ? "Tiled" : (gpuStrided_ ? "LinearStrided" : "Linear"),
					width_, height_, std::uint32_t(result));
				GxmMemory::ReleaseSurface(gpuBlock_);
				return false;
			}

			gpuValid_ = true;
			samplerDirty_ = true;
			// A render target's texels are produced by the GPU, so there is nothing to upload into it - and
			// uploading the (empty) host store would wipe a target that has already been rendered into
			contentsDirty_ = !isRenderTarget_;
		}

		if (samplerDirty_) {
			ApplySamplerState();
			samplerDirty_ = false;
		}
		if (contentsDirty_) {
			UploadPixels();
			contentsDirty_ = false;
		}
		return gpuValid_;
	}

	void GxmTexture::ApplySamplerState() const
	{
		// A strided texture has one filter field for both directions and no mip filter at all; programming
		// either of the two rejects the call with SCE_GXM_ERROR_UNSUPPORTED, so only the shared one is set
		sceGxmTextureSetMagFilter(&gpuTexture_, TranslateFilter(magFilter_));
		if (!gpuStrided_) {
			sceGxmTextureSetMinFilter(&gpuTexture_, TranslateFilter(minFilter_));
			sceGxmTextureSetMipFilter(&gpuTexture_, SCE_GXM_TEXTURE_MIP_FILTER_DISABLED);
			// None of these textures has a mip chain, and the level count has to say so rather than be left at
			// whatever the init call defaulted to. It matters where a shader's texture coordinates jump between
			// neighbouring pixels: the textured background mirrors its layer about the horizon, so the row at
			// the fold has a derivative hundreds of times the rest of the image, and a level of detail chosen
			// from that reaches past the only level that exists. OpenGL clamps to the base level for an
			// incomplete chain, which is why that row is a black line here and not there.
			sceGxmTextureSetMipmapCount(&gpuTexture_, 1);
			sceGxmTextureSetLodBias(&gpuTexture_, 0);
		}
		// A linear layout - which is what every texture here has, strided or not - is documented to support
		// fewer addressing modes than a tiled or swizzled one, and a rejected mode leaves whatever the texture
		// was initialized with in place. That shows up as a seam wherever sampling reaches an edge: a repeating
		// background that clamps instead of wrapping draws a line at the wrap, and a blur that reads past an
		// edge picks up whatever is outside. Reported once per mode so the log says which textures wanted what.
		const SceGxmTextureAddrMode addrMode = TranslateWrap(wrap_);
		const std::int32_t uResult = sceGxmTextureSetUAddrMode(&gpuTexture_, addrMode);
		const std::int32_t vResult = sceGxmTextureSetVAddrMode(&gpuTexture_, addrMode);

		if (uResult < 0 || vResult < 0) {
			static std::uint32_t reportedModes = 0;
			const std::uint32_t modeBit = (1u << std::uint32_t(addrMode));
			if ((reportedModes & modeBit) == 0) {
				reportedModes |= modeBit;
				LOGW("A {}x{} {} texture rejected addressing mode {} with 0x{:.8x}/0x{:.8x}, so it keeps the one "
					"it was created with", width_, height_, gpuStrided_ ? "linear-strided" : "linear",
					std::uint32_t(addrMode), std::uint32_t(uResult), std::uint32_t(vResult));
			}
		}
	}

	void GxmTexture::UploadPixels() const
	{
		if (!gpuBlock_.IsValid() || pixels_.empty()) {
			return;
		}

		std::uint8_t* dst = static_cast<std::uint8_t*>(gpuBlock_.Base);
		const std::uint32_t rowBytes = std::uint32_t(width_) * 4u;
		if (IsIdentitySwizzle()) {
			if (gpuStride_ == rowBytes) {
				// Same stride on both sides, so the whole image is one copy
				std::memcpy(dst, pixels_.data(), std::size_t(rowBytes) * std::size_t(height_));
			} else {
				// A padded stride (a colour attachment) is filled row by row, leaving its padding untouched
				for (std::int32_t y = 0; y < height_; y++) {
					std::memcpy(dst + std::size_t(y) * gpuStride_, pixels_.data() + std::size_t(y) * rowBytes, rowBytes);
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
		for (std::int32_t y = 0; y < height_; y++) {
			const std::uint8_t* in = pixels_.data() + std::size_t(y) * rowBytes;
			std::uint8_t* out = dst + std::size_t(y) * gpuStride_;
			for (std::int32_t x = 0; x < width_; x++, in += 4, out += 4) {
				out[0] = pick(swizzle_[0], in);
				out[1] = pick(swizzle_[1], in);
				out[2] = pick(swizzle_[2], in);
				out[3] = pick(swizzle_[3], in);
			}
		}
	}

	const SceGxmTexture* GxmTexture::GetGxmTexture() const
	{
		return (EnsureGpuTexture() ? &gpuTexture_ : nullptr);
	}

	void* GxmTexture::GetSurfaceData() const
	{
		return (EnsureGpuTexture() ? gpuBlock_.Base : nullptr);
	}

	bool GxmTexture::Bind(std::uint32_t textureUnit) const
	{
		textureUnit_ = textureUnit;
		GxmDevice::BindTexture(textureUnit, this);
		return true;
	}

	bool GxmTexture::Unbind() const
	{
		GxmDevice::BindTexture(textureUnit_, nullptr);
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
		if (data != nullptr && !pixels_.empty()) {
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

	void GxmTexture::TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0 || data == nullptr || pixels_.empty()) {
			return;
		}
		if (xoffset < 0 || yoffset < 0 || xoffset + width > width_ || yoffset + height > height_) {
			return;
		}

		contentsDirty_ = true;
		const std::int32_t srcBpp = BytesPerPixel(format);
		const std::int32_t dstBpp = BytesPerPixel(format_);
		const std::uint8_t* src = static_cast<const std::uint8_t*>(data);
		for (std::int32_t y = 0; y < height; y++) {
			CopyExpandRow(pixels_.data() + std::size_t(yoffset + y) * strideBytes_ + std::size_t(xoffset) * dstBpp,
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
		if (level != 0 || pixels == nullptr || width_ <= 0 || height_ <= 0) {
			return;
		}

		// A render target's texels live only in its GPU copy, and the GPU has to be done writing them first
		const std::uint8_t* source = pixels_.data();
		if (isRenderTarget_ && gpuBlock_.IsValid()) {
			GxmDevice::FinishScene();
			if (SceGxmContext* context = GxmDevice::GetContext()) {
				sceGxmFinish(context);
			}
			source = static_cast<const std::uint8_t*>(gpuBlock_.Base);
		}
		if (source == nullptr) {
			return;
		}

		const std::int32_t dstBpp = BytesPerPixel(format);
		const std::int32_t srcBpp = BytesPerPixel(format_);
		// A render target's rows may be padded to the stride its colour surface needed
		const std::size_t sourceStride = (source == pixels_.data()
			? std::size_t(strideBytes_) : std::size_t(gpuStride_));
		std::uint8_t* dst = static_cast<std::uint8_t*>(pixels);
		if (dstBpp == srcBpp) {
			const std::size_t rowBytes = std::size_t(width_) * std::size_t(srcBpp);
			for (std::int32_t y = 0; y < height_; y++) {
				std::memcpy(dst + std::size_t(y) * rowBytes, source + std::size_t(y) * sourceStride, rowBytes);
			}
			return;
		}
		// Narrowing readback (an RGBA8 store read back as RGB8): drop the trailing channels per texel
		const std::int32_t shared = (srcBpp < dstBpp ? srcBpp : dstBpp);
		for (std::int32_t y = 0; y < height_; y++) {
			const std::uint8_t* in = source + std::size_t(y) * sourceStride;
			std::uint8_t* out = dst + std::size_t(y) * std::size_t(width_) * std::size_t(dstBpp);
			for (std::int32_t x = 0; x < width_; x++, in += srcBpp, out += dstBpp) {
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
		if (minFilter_ != filter) {
			minFilter_ = filter;
			samplerDirty_ = true;
		}
	}

	void GxmTexture::SetMagFiltering(nCine::SamplerFilter filter)
	{
		if (magFilter_ != filter) {
			magFilter_ = filter;
			samplerDirty_ = true;
		}
	}

	void GxmTexture::SetWrap(SamplerWrapping wrap)
	{
		if (wrap_ != wrap) {
			wrap_ = wrap;
			samplerDirty_ = true;
		}
	}

	void GxmTexture::SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a)
	{
		if (swizzle_[0] == r && swizzle_[1] == g && swizzle_[2] == b && swizzle_[3] == a) {
			return;
		}
		swizzle_[0] = r;
		swizzle_[1] = g;
		swizzle_[2] = b;
		swizzle_[3] = a;
		// The swizzle is baked into the uploaded texels, so the GPU copy is now stale
		contentsDirty_ = true;
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
