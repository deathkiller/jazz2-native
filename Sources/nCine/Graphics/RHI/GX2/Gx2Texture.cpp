#if defined(WITH_RHI_GX2)

#include "Gx2Texture.h"
#include "Gx2Device.h"
#include "Gx2RenderTarget.h"

#include "../../../../Main.h"

#include <cstring>

#include <gx2/mem.h>
#include <gx2/shaders.h>
#include <gx2/utils.h>

namespace nCine::RHI::GX2
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
			@brief Translates the engine's filters into the one min/mag mode `GX2InitSampler()` takes

			The store is single-level, so a mipmapped minification mode is reduced to its base filter. GX2
			sets minification and magnification together; linear wins if either asks for it, which is what
			the engine's textures do anyway (both filters are always set to the same value).
		*/
		inline GX2TexXYFilterMode TranslateFilter(nCine::SamplerFilter minFilter, nCine::SamplerFilter magFilter)
		{
			const bool linear = (magFilter == nCine::SamplerFilter::Linear ||
				minFilter == nCine::SamplerFilter::Linear ||
				minFilter == nCine::SamplerFilter::LinearMipmapNearest ||
				minFilter == nCine::SamplerFilter::LinearMipmapLinear);
			return (linear ? GX2_TEX_XY_FILTER_MODE_LINEAR : GX2_TEX_XY_FILTER_MODE_POINT);
		}

		inline GX2TexClampMode TranslateWrap(SamplerWrapping wrap)
		{
			switch (wrap) {
				case SamplerWrapping::Repeat: return GX2_TEX_CLAMP_MODE_WRAP;
				case SamplerWrapping::MirroredRepeat: return GX2_TEX_CLAMP_MODE_MIRROR;
				default: return GX2_TEX_CLAMP_MODE_CLAMP;
			}
		}

		inline std::uint32_t TranslateSwizzle(SwizzleChannel channel)
		{
			switch (channel) {
				case SwizzleChannel::Red: return GX2_SQ_SEL_R;
				case SwizzleChannel::Green: return GX2_SQ_SEL_G;
				case SwizzleChannel::Blue: return GX2_SQ_SEL_B;
				case SwizzleChannel::Alpha: return GX2_SQ_SEL_A;
				case SwizzleChannel::Zero: return GX2_SQ_SEL_0;
				default: return GX2_SQ_SEL_1;
			}
		}

	}

	namespace
	{
		// Textures whose GPU copy is in MEM1, so EnterBackground() can free them all; MEM1 holds only a handful
		constexpr std::uint32_t MaxMem1Textures = 32;
		const Gx2Texture* _mem1Textures[MaxMem1Textures];
		std::uint32_t _mem1TextureCount = 0;

		void ForgetMem1Texture(const Gx2Texture* texture)
		{
			for (std::uint32_t i = 0; i < _mem1TextureCount; i++) {
				if (_mem1Textures[i] == texture) {
					_mem1Textures[i] = _mem1Textures[--_mem1TextureCount];
					return;
				}
			}
		}
	}

	std::uint32_t Gx2Texture::_nextHandle = 1;

	Gx2Texture::Gx2Texture(TextureTarget target)
		: _handle(_nextHandle++), _target(target), _format(PixelFormat::Unknown), _uploadFormat(PixelFormat::Unknown),
			_width(0), _height(0), _strideBytes(0),
			_minFilter(nCine::SamplerFilter::Nearest), _magFilter(nCine::SamplerFilter::Nearest), _wrap(SamplerWrapping::ClampToEdge),
			_textureUnit(0), _isRenderTarget(false), _preferFastMemory(false), _gpuValid(false), _contentsDirty(false), _streamingDirty(false),
			_samplerDirty(true), _lastUse(0)
	{
		_swizzle[0] = SwizzleChannel::Red;
		_swizzle[1] = SwizzleChannel::Green;
		_swizzle[2] = SwizzleChannel::Blue;
		_swizzle[3] = SwizzleChannel::Alpha;
		std::memset(&_gpuTexture, 0, sizeof(_gpuTexture));
		std::memset(&_sampler, 0, sizeof(_sampler));
	}

	Gx2Texture::~Gx2Texture()
	{
		// The device tracks bound textures by pointer, so it has to forget this one before the memory goes
		Gx2Device::UnbindTexture(this);
		// So does a render target that still has it attached; its destructor would otherwise call back into
		// this freed texture
		for (Gx2RenderTarget* target : _attachedTargets) {
			target->ForgetColorTexture(*this);
		}
		ReleaseGpu();
	}

	void Gx2Texture::ReleaseGpu() const
	{
		if (_gpuBlock.IsMem1()) {
			ForgetMem1Texture(this);
		}
		if (_gpuBlock.IsValid()) {
			// The GPU may still be reading these texels from a frame that has not finished, so the block is
			// handed to the device's retirement list rather than freed under the hardware
			Gx2Device::RetireBlock(_gpuBlock);
		}
		_gpuValid = false;
		_lastUse = 0;
	}

	std::int32_t Gx2Texture::BytesPerPixel(PixelFormat format)
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

	std::uint32_t Gx2Texture::BuildCompMap() const
	{
		return (TranslateSwizzle(_swizzle[0]) << 24) | (TranslateSwizzle(_swizzle[1]) << 16) |
			(TranslateSwizzle(_swizzle[2]) << 8) | TranslateSwizzle(_swizzle[3]);
	}

	void Gx2Texture::Allocate(PixelFormat format, std::int32_t width, std::int32_t height)
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

	void Gx2Texture::SetRenderTarget(bool isRenderTarget)
	{
		if (isRenderTarget == _isRenderTarget) {
			return;
		}
		_isRenderTarget = isRenderTarget;
		// A colour attachment is tiled and an ordinary texture is linear (see the class documentation), so the
		// GPU copy is rebuilt in the other layout; a target's texels stop coming from the host store
		ReleaseGpu();
		_contentsDirty = !isRenderTarget;
	}

	void Gx2Texture::AddAttachedTarget(Gx2RenderTarget* target)
	{
		for (Gx2RenderTarget* attached : _attachedTargets) {
			if (attached == target) {
				return;
			}
		}
		_attachedTargets.push_back(target);
	}

	void Gx2Texture::RemoveAttachedTarget(Gx2RenderTarget* target)
	{
		for (std::size_t i = 0; i < _attachedTargets.size(); i++) {
			if (_attachedTargets[i] == target) {
				_attachedTargets.erase(_attachedTargets.begin() + i);
				return;
			}
		}
	}

	void Gx2Texture::SetPreferFastMemory(bool value)
	{
		if (value == _preferFastMemory) {
			return;
		}
		_preferFastMemory = value;
		if (_isRenderTarget) {
			// Moved on the next use; a render target's texels are redrawn, so nothing is copied
			ReleaseGpu();
		}
	}

	void Gx2Texture::ReleaseMem1Copies()
	{
		for (std::uint32_t i = 0; i < _mem1TextureCount; i++) {
			const Gx2Texture* texture = _mem1Textures[i];
			Gx2Memory::Free(texture->_gpuBlock);
			texture->_gpuValid = false;
			texture->_lastUse = 0;
		}
		_mem1TextureCount = 0;
	}

	bool Gx2Texture::EnsureGpuTexture() const
	{
		if (_width <= 0 || _height <= 0 || _format != PixelFormat::RGBA8) {
			// Only the promoted RGBA8 store has a GPU-side counterpart; the float and packed formats are
			// accepted by the uploads but never sampled by this backend's shaders
			return false;
		}

		if (_gpuValid && _contentsDirty && !_isRenderTarget && !Gx2Device::IsUseRetired(_lastUse)) {
			// A queued draw may still sample the current copy (the palette is rewritten while the previous frame is
			// on the GPU, for example), so the refresh goes into a new block rather than under the GPU
			ReleaseGpu();
		}

		if (!_gpuValid) {
			std::memset(&_gpuTexture, 0, sizeof(_gpuTexture));
			GX2Surface& surface = _gpuTexture.surface;
			surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
			surface.width = std::uint32_t(_width);
			surface.height = std::uint32_t(_height);
			surface.depth = 1;
			surface.mipLevels = 1;
			surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
			surface.aa = GX2_AA_MODE1X;
			surface.use = (_isRenderTarget ? GX2_SURFACE_USE_TEXTURE_COLOR_BUFFER_TV : GX2_SURFACE_USE_TEXTURE);
			surface.tileMode = (_isRenderTarget ? GX2_TILE_MODE_DEFAULT : GX2_TILE_MODE_LINEAR_ALIGNED);
			GX2CalcSurfaceSizeAndAlignment(&surface);

			if (_gpuBlock.Size < surface.imageSize) {
				if (_gpuBlock.IsMem1()) {
					ForgetMem1Texture(this);
				}
				Gx2Device::RetireBlock(_gpuBlock);
				if (_isRenderTarget && _preferFastMemory && _mem1TextureCount < MaxMem1Textures && Gx2Device::CanAllocMem1()) {
					_gpuBlock = Gx2Memory::AllocColorSurfaceMem1(surface);
					if (_gpuBlock.IsValid()) {
						_mem1Textures[_mem1TextureCount++] = this;
					}
				}
				if (!_gpuBlock.IsValid()) {
					_gpuBlock = Gx2Memory::Alloc(surface.imageSize, surface.alignment);
				}
				if (!_gpuBlock.IsValid()) {
					LOGE("Failed to allocate {} bytes for a {}x{} texture", surface.imageSize, _width, _height);
					return false;
				}
			}
			surface.image = _gpuBlock.Base;

			_gpuTexture.viewFirstMip = 0;
			_gpuTexture.viewNumMips = 1;
			_gpuTexture.viewFirstSlice = 0;
			_gpuTexture.viewNumSlices = 1;
			_gpuTexture.compMap = BuildCompMap();
			GX2InitTextureRegs(&_gpuTexture);
			_gpuValid = true;

			if (_isRenderTarget) {
				// Nothing has been drawn into it yet; start from black like a fresh GL texture would
				std::memset(_gpuBlock.Base, 0, surface.imageSize);
				Gx2Memory::FlushCpuWrites(_gpuBlock.Base, surface.imageSize, GX2_INVALIDATE_MODE_TEXTURE | GX2_INVALIDATE_MODE_COLOR_BUFFER);
			} else {
				_contentsDirty = true;
			}
		}

		if (_contentsDirty && !_isRenderTarget) {
			UploadPixels();
			_contentsDirty = false;
			_streamingDirty = false;
		}
		if (_streamingDirty) {
			Gx2Memory::FlushCpuWrites(_gpuTexture.surface.image, _gpuTexture.surface.imageSize, GX2_INVALIDATE_MODE_TEXTURE);
			_streamingDirty = false;
		}
		return true;
	}

	void Gx2Texture::UploadPixels() const
	{
		if (!_gpuValid || _pixels.empty()) {
			return;
		}
		const std::size_t rowBytes = std::size_t(_strideBytes);
		const std::size_t pitchBytes = std::size_t(_gpuTexture.surface.pitch) * 4;
		std::uint8_t* dst = static_cast<std::uint8_t*>(_gpuTexture.surface.image);
		if (pitchBytes == rowBytes) {
			std::memcpy(dst, _pixels.data(), rowBytes * std::size_t(_height));
		} else {
			for (std::int32_t y = 0; y < _height; y++) {
				std::memcpy(dst + std::size_t(y) * pitchBytes, _pixels.data() + std::size_t(y) * rowBytes, rowBytes);
			}
		}
		Gx2Memory::FlushCpuWrites(dst, _gpuTexture.surface.imageSize, GX2_INVALIDATE_MODE_TEXTURE);
	}

	bool Gx2Texture::ApplySamplerState(std::uint32_t textureUnit) const
	{
		const GX2Texture* texture = GetGx2Texture();
		if (texture == nullptr) {
			return false;
		}
		if (_samplerDirty) {
			GX2InitSampler(&_sampler, TranslateWrap(_wrap), TranslateFilter(_minFilter, _magFilter));
			_samplerDirty = false;
		}
		GX2SetPixelTexture(texture, textureUnit);
		GX2SetPixelSampler(&_sampler, textureUnit);
		return true;
	}

	const GX2Texture* Gx2Texture::GetGx2Texture() const
	{
		return (EnsureGpuTexture() ? &_gpuTexture : nullptr);
	}

	const GX2Surface* Gx2Texture::GetSurface() const
	{
		return (EnsureGpuTexture() ? &_gpuTexture.surface : nullptr);
	}

	bool Gx2Texture::Bind(std::uint32_t textureUnit) const
	{
		_textureUnit = textureUnit;
		Gx2Device::BindTexture(textureUnit, this);
		return true;
	}

	bool Gx2Texture::Unbind() const
	{
		Gx2Device::BindTexture(_textureUnit, nullptr);
		return true;
	}

	bool Gx2Texture::Unbind(std::uint32_t textureUnit)
	{
		Gx2Device::BindTexture(textureUnit, nullptr);
		return true;
	}

	void Gx2Texture::TexImage2D(std::int32_t level, PixelFormat format, bool bgr, std::int32_t width, std::int32_t height, const void* data)
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

	void Gx2Texture::TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data)
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

	void Gx2Texture::TexStorage2D(std::int32_t levels, PixelFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(levels);
		Allocate(format, width, height);
	}

	void Gx2Texture::CompressedTexImage2D(std::int32_t level, PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t imageSize, const void* data)
	{
		// GX2 does have BC sampling, but the engine's console content is delivered uncompressed and
		// nothing in the pipeline reaches this path; accepted as a no-op rather than half-implemented
		static_cast<void>(level);
		static_cast<void>(format);
		static_cast<void>(width);
		static_cast<void>(height);
		static_cast<void>(imageSize);
		static_cast<void>(data);
	}

	void Gx2Texture::CompressedTexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, std::int32_t imageSize, const void* data)
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

	void Gx2Texture::GetTexImage(std::int32_t level, PixelFormat format, bool bgr, void* pixels)
	{
		static_cast<void>(bgr);
		if (level != 0 || pixels == nullptr || _width <= 0 || _height <= 0) {
			return;
		}

		// TODO: a render target's texels live only in its tiled GPU copy, which would need GX2CopySurface() into
		// a linear surface to read back; until then this returns the host store, which is blank for a target
		const std::uint8_t* source = _pixels.data();
		std::size_t sourceStride = std::size_t(_strideBytes);
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

	void* Gx2Texture::MapStreamingTexels(std::int32_t& strideBytes)
	{
		// A render target's texels belong to the GPU, and only an RGBA8 store has a GPU copy at all. A format
		// promoted to RGBA8 (R8, RG8, RGB8) isn't stored verbatim, so the caller's texels would not line up.
		if (_isRenderTarget || _format != PixelFormat::RGBA8 || _uploadFormat != PixelFormat::RGBA8) {
			return nullptr;
		}
		if (!EnsureGpuTexture()) {
			return nullptr;
		}
		// The caller writes straight into the GPU copy. Cinematics alternates between two textures, so the frame
		// that sampled this one is normally done already. If the wait times out (a GPU hang), the texture stays
		// protected and keeps its token, so the next map waits again.
		if (!Gx2Device::WaitForUse(_lastUse)) {
			return nullptr;
		}
		_lastUse = 0;

		// The caller overwrites the whole surface every frame, so the host store is now stale by definition;
		// clearing the dirty flag stops the next bind from copying it back over what was just written, and the
		// streaming flag makes that bind flush the caller's writes out of the CPU cache instead
		_contentsDirty = false;
		_streamingDirty = true;

		strideBytes = std::int32_t(_gpuTexture.surface.pitch * 4);
		return _gpuTexture.surface.image;
	}

	void Gx2Texture::SetMinFiltering(nCine::SamplerFilter filter)
	{
		_minFilter = filter;
		_samplerDirty = true;
	}

	void Gx2Texture::SetMagFiltering(nCine::SamplerFilter filter)
	{
		_magFilter = filter;
		_samplerDirty = true;
	}

	void Gx2Texture::SetWrap(SamplerWrapping wrap)
	{
		_wrap = wrap;
		_samplerDirty = true;
	}

	void Gx2Texture::SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a)
	{
		// Only the remap word changes; the texels stay as they were uploaded, which is what makes this cheap
		// enough for the pipeline to set per material rather than per texture creation
		_swizzle[0] = r;
		_swizzle[1] = g;
		_swizzle[2] = b;
		_swizzle[3] = a;
		if (_gpuValid) {
			_gpuTexture.compMap = BuildCompMap();
			GX2InitTextureRegs(&_gpuTexture);
		}
	}

	void Gx2Texture::SetMaxLevel(std::int32_t maxLevel)
	{
		static_cast<void>(maxLevel);
	}

	void Gx2Texture::SetUnpackAlignment(std::int32_t alignment)
	{
		static_cast<void>(alignment);
	}

	void Gx2Texture::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}

#endif
