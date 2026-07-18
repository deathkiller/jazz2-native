#include "D3D11Texture.h"
#include "D3D11Device.h"

#include <cstring>

#include <d3d11.h>

#include <Asserts.h>

namespace nCine::RhiD3D11
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

	std::uint32_t D3D11Texture::nextHandle_ = 1;

	D3D11Texture::D3D11Texture(TextureTarget target)
		: handle_(nextHandle_++), target_(target), format_(PixelFormat::Unknown), uploadFormat_(PixelFormat::Unknown),
			width_(0), height_(0), strideBytes_(0),
			minFilter_(nCine::SamplerFilter::Nearest), magFilter_(nCine::SamplerFilter::Nearest), wrap_(SamplerWrapping::ClampToEdge),
			textureUnit_(0), isRenderTarget_(false),
			gpuTexture_(nullptr), srv_(nullptr), sampler_(nullptr), contentsDirty_(false), hasCpuData_(false),
			samplerMinFilter_(nCine::SamplerFilter::Nearest), samplerFilter_(nCine::SamplerFilter::Nearest), samplerWrap_(SamplerWrapping::ClampToEdge)
	{
		swizzle_[0] = SwizzleChannel::Red;
		swizzle_[1] = SwizzleChannel::Green;
		swizzle_[2] = SwizzleChannel::Blue;
		swizzle_[3] = SwizzleChannel::Alpha;
	}

	D3D11Texture::~D3D11Texture()
	{
		// Unbind from the device first so a later draw can't dereference this freed texture via boundTextures_
		// (crashed in BindTextures during splitscreen level changes)
		D3D11Device::UnbindTexture(this);
		ReleaseGpu();
	}

	void D3D11Texture::ReleaseGpu() const
	{
		if (srv_ != nullptr) { srv_->Release(); srv_ = nullptr; }
		if (gpuTexture_ != nullptr) { gpuTexture_->Release(); gpuTexture_ = nullptr; }
		if (sampler_ != nullptr) { sampler_->Release(); sampler_ = nullptr; }
	}

	bool D3D11Texture::IsIdentitySwizzle() const
	{
		return (swizzle_[0] == SwizzleChannel::Red && swizzle_[1] == SwizzleChannel::Green &&
			swizzle_[2] == SwizzleChannel::Blue && swizzle_[3] == SwizzleChannel::Alpha);
	}

	const std::uint8_t* D3D11Texture::SwizzledUploadPixels() const
	{
		// The store is always RGBA8 (4 bpp) after Allocate()'s promotion. D3D11's base SRV cannot remap
		// channels the way GL's GL_TEXTURE_SWIZZLE_* does, so a non-identity swizzle (e.g. the palette-index
		// RG8 textures set A<-Green so the shader's `src.a` reads the packed alpha byte) is baked into the
		// uploaded texels here. Without this, `src.a` would always be 1.0 and RG8 sprites (gems, pre-packed
		// index+alpha) would lose their transparency; R8 textures keep the identity swizzle and are untouched.
		if (pixels_.empty() || IsIdentitySwizzle()) {
			return pixels_.data();
		}
		auto pick = [](SwizzleChannel channel, const std::uint8_t* texel) -> std::uint8_t {
			switch (channel) {
				case SwizzleChannel::Red:	return texel[0];
				case SwizzleChannel::Green:	return texel[1];
				case SwizzleChannel::Blue:	return texel[2];
				case SwizzleChannel::Alpha:	return texel[3];
				case SwizzleChannel::Zero:	return 0;
				case SwizzleChannel::One:	return 255;
				default:					return texel[0];
			}
		};
		swizzledPixels_.resize(pixels_.size());
		const std::size_t texelCount = pixels_.size() / 4;
		for (std::size_t i = 0; i < texelCount; i++) {
			const std::uint8_t* in = &pixels_[i * 4];
			std::uint8_t* out = &swizzledPixels_[i * 4];
			out[0] = pick(swizzle_[0], in);
			out[1] = pick(swizzle_[1], in);
			out[2] = pick(swizzle_[2], in);
			out[3] = pick(swizzle_[3], in);
		}
		return swizzledPixels_.data();
	}

	void D3D11Texture::EnsureGpuTexture() const
	{
		ID3D11Device* device = D3D11Device::GetD3DDevice();
		if (device == nullptr || width_ <= 0 || height_ <= 0) {
			return;
		}

		if (gpuTexture_ != nullptr && !contentsDirty_) {
			return;
		}

		// A CPU re-upload into an existing same-size texture just refreshes its contents
		if (gpuTexture_ != nullptr && contentsDirty_ && !isRenderTarget_ && hasCpuData_ && !pixels_.empty()) {
			ID3D11DeviceContext* context = D3D11Device::GetD3DContext();
			if (context != nullptr) {
				context->UpdateSubresource(gpuTexture_, 0, nullptr, SwizzledUploadPixels(), std::uint32_t(strideBytes_), 0);
				contentsDirty_ = false;
				return;
			}
		}

		// (Re)create the texture from scratch. The host store is always RGBA8 after the promotion in
		// Allocate(), so a single DXGI format covers every runtime texture.
		if (srv_ != nullptr) { srv_->Release(); srv_ = nullptr; }
		if (gpuTexture_ != nullptr) { gpuTexture_->Release(); gpuTexture_ = nullptr; }

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = static_cast<UINT>(width_);
		desc.Height = static_cast<UINT>(height_);
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (isRenderTarget_ ? D3D11_BIND_RENDER_TARGET : 0u);
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;

		D3D11_SUBRESOURCE_DATA init = {};
		const bool hasInit = (hasCpuData_ && !isRenderTarget_ && !pixels_.empty());
		if (hasInit) {
			init.pSysMem = SwizzledUploadPixels();
			init.SysMemPitch = static_cast<UINT>(strideBytes_);
		}

		if (FAILED(device->CreateTexture2D(&desc, hasInit ? &init : nullptr, &gpuTexture_))) {
			gpuTexture_ = nullptr;
			return;
		}
		device->CreateShaderResourceView(gpuTexture_, nullptr, &srv_);
		contentsDirty_ = false;
	}

	ID3D11ShaderResourceView* D3D11Texture::GetSRV() const
	{
		EnsureGpuTexture();
		return srv_;
	}

	ID3D11Texture2D* D3D11Texture::GetOrCreateTexture2D() const
	{
		EnsureGpuTexture();
		return gpuTexture_;
	}

	ID3D11SamplerState* D3D11Texture::GetSampler() const
	{
		ID3D11Device* device = D3D11Device::GetD3DDevice();
		if (device == nullptr) {
			return nullptr;
		}
		if (sampler_ != nullptr && samplerFilter_ == magFilter_ && samplerMinFilter_ == minFilter_ && samplerWrap_ == wrap_) {
			return sampler_;
		}
		if (sampler_ != nullptr) { sampler_->Release(); sampler_ = nullptr; }

		D3D11_SAMPLER_DESC desc = {};
		// Compose the filter from both the minification and magnification modes (mip mode is irrelevant, the
		// backend only ever stores level 0)
		const bool magLinear = (magFilter_ == nCine::SamplerFilter::Linear ||
			magFilter_ == nCine::SamplerFilter::LinearMipmapNearest || magFilter_ == nCine::SamplerFilter::LinearMipmapLinear);
		const bool minLinear = (minFilter_ == nCine::SamplerFilter::Linear ||
			minFilter_ == nCine::SamplerFilter::LinearMipmapNearest || minFilter_ == nCine::SamplerFilter::LinearMipmapLinear);
		if (minLinear && magLinear) {
			desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		} else if (minLinear) {
			desc.Filter = D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT;
		} else if (magLinear) {
			desc.Filter = D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
		} else {
			desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		}
		D3D11_TEXTURE_ADDRESS_MODE address;
		switch (wrap_) {
			case SamplerWrapping::Repeat: address = D3D11_TEXTURE_ADDRESS_WRAP; break;
			case SamplerWrapping::MirroredRepeat: address = D3D11_TEXTURE_ADDRESS_MIRROR; break;
			default: address = D3D11_TEXTURE_ADDRESS_CLAMP; break;
		}
		desc.AddressU = address;
		desc.AddressV = address;
		desc.AddressW = address;
		desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		desc.MinLOD = 0;
		desc.MaxLOD = D3D11_FLOAT32_MAX;
		device->CreateSamplerState(&desc, &sampler_);
		samplerMinFilter_ = minFilter_;
		samplerFilter_ = magFilter_;
		samplerWrap_ = wrap_;
		return sampler_;
	}

	std::int32_t D3D11Texture::BytesPerPixel(PixelFormat format)
	{
		switch (format) {
			case PixelFormat::R8: return 1;
			case PixelFormat::RG8: return 2;
			case PixelFormat::RGB8: return 3;
			case PixelFormat::RGBA8: return 4;
			default: return 0;
		}
	}

	void D3D11Texture::Allocate(PixelFormat format, std::int32_t width, std::int32_t height)
	{
		// Keep a self-consistent 4-byte-per-texel store for the runtime formats: promote the narrower ones
		// (RGB8 render targets and the palette-index formats R8 / RG8) to an RGBA8 store, remembering the
		// original in uploadFormat_. Mirrors the software backend's promotion so the backend can lift the same
		// bytes into an `ID3D11Texture2D` without a separate widening step.
		uploadFormat_ = format;
		format_ = (format == PixelFormat::RGB8 || format == PixelFormat::R8 || format == PixelFormat::RG8) ? PixelFormat::RGBA8 : format;
		width_ = width;
		height_ = height;
		const std::int32_t bpp = BytesPerPixel(format_);
		strideBytes_ = width * bpp;
		pixels_.assign(std::size_t(strideBytes_) * std::size_t(height > 0 ? height : 0), std::uint8_t(0));
		// The size/format changed, so any existing GPU texture must be rebuilt on the next bind
		if (srv_ != nullptr) { srv_->Release(); srv_ = nullptr; }
		if (gpuTexture_ != nullptr) { gpuTexture_->Release(); gpuTexture_ = nullptr; }
		contentsDirty_ = true;
	}

	bool D3D11Texture::Bind(std::uint32_t textureUnit) const
	{
		textureUnit_ = textureUnit;
		D3D11Device::BindTexture(textureUnit, this);
		return true;
	}

	bool D3D11Texture::Unbind() const
	{
		D3D11Device::BindTexture(textureUnit_, nullptr);
		return true;
	}

	bool D3D11Texture::Unbind(std::uint32_t textureUnit)
	{
		D3D11Device::BindTexture(textureUnit, nullptr);
		return true;
	}

	void D3D11Texture::TexImage2D(std::int32_t level, PixelFormat format, bool bgr, std::int32_t width, std::int32_t height, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0) {
			return;
		}
		Allocate(format, width, height);
		if (data != nullptr && !pixels_.empty()) {
			hasCpuData_ = true;
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

	void D3D11Texture::TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0 || data == nullptr || pixels_.empty()) {
			return;
		}
		hasCpuData_ = true;
		contentsDirty_ = true;
		const std::int32_t srcBpp = BytesPerPixel(format);
		const std::int32_t dstBpp = BytesPerPixel(format_);
		for (std::int32_t y = 0; y < height; y++) {
			const std::int32_t dstY = yoffset + y;
			if (dstY < 0 || dstY >= height_) {
				continue;
			}
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

	void D3D11Texture::TexStorage2D(std::int32_t levels, PixelFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(levels);
		Allocate(format, width, height);
	}

	void D3D11Texture::CompressedTexImage2D(std::int32_t level, PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t imageSize, const void* data)
	{
		static_cast<void>(level);
		static_cast<void>(format);
		static_cast<void>(width);
		static_cast<void>(height);
		static_cast<void>(imageSize);
		static_cast<void>(data);
	}

	void D3D11Texture::CompressedTexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, std::int32_t imageSize, const void* data)
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

	void D3D11Texture::GetTexImage(std::int32_t level, PixelFormat format, bool bgr, void* pixels)
	{
		static_cast<void>(level);
		static_cast<void>(format);
		static_cast<void>(bgr);
		if (pixels == nullptr) {
			return;
		}

		// A render target's contents only exist on the GPU (the host store is never written by draws), so read
		// it back through a staging copy; plain textures return the host store directly
		if (isRenderTarget_ && gpuTexture_ != nullptr) {
			ID3D11Device* device = D3D11Device::GetD3DDevice();
			ID3D11DeviceContext* context = D3D11Device::GetD3DContext();
			if (device != nullptr && context != nullptr) {
				D3D11_TEXTURE2D_DESC desc = {};
				gpuTexture_->GetDesc(&desc);
				desc.Usage = D3D11_USAGE_STAGING;
				desc.BindFlags = 0;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				desc.MiscFlags = 0;
				ID3D11Texture2D* staging = nullptr;
				if (SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging))) {
					context->CopyResource(staging, gpuTexture_);
					D3D11_MAPPED_SUBRESOURCE mapped;
					if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
						const std::uint32_t rowBytes = std::uint32_t(width_) * 4;
						std::uint8_t* dst = static_cast<std::uint8_t*>(pixels);
						const std::uint8_t* src = static_cast<const std::uint8_t*>(mapped.pData);
						for (std::int32_t y = 0; y < height_; y++) {
							std::memcpy(dst + std::size_t(y) * rowBytes, src + std::size_t(y) * mapped.RowPitch, rowBytes);
						}
						context->Unmap(staging, 0);
						staging->Release();
						return;
					}
					staging->Release();
				}
			}
		}

		if (!pixels_.empty()) {
			std::memcpy(pixels, pixels_.data(), pixels_.size());
		}
	}

	void D3D11Texture::SetMinFiltering(nCine::SamplerFilter filter)
	{
		minFilter_ = filter;
	}

	void D3D11Texture::SetMagFiltering(nCine::SamplerFilter filter)
	{
		magFilter_ = filter;
	}

	void D3D11Texture::SetWrap(SamplerWrapping wrap)
	{
		wrap_ = wrap;
	}

	void D3D11Texture::SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a)
	{
		swizzle_[0] = r;
		swizzle_[1] = g;
		swizzle_[2] = b;
		swizzle_[3] = a;
		// The swizzle is baked into the uploaded texels (D3D11 has no SRV channel remap), so a change must
		// rebuild the GPU texture; it is usually set right after the upload, before the first bind.
		contentsDirty_ = true;
	}

	void D3D11Texture::SetMaxLevel(std::int32_t maxLevel)
	{
		static_cast<void>(maxLevel);
	}

	void D3D11Texture::SetUnpackAlignment(std::int32_t alignment)
	{
		static_cast<void>(alignment);
	}

	void D3D11Texture::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
