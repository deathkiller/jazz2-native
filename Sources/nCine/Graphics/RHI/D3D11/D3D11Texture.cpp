#include "D3D11Texture.h"
#include "D3D11Device.h"

#include <cstring>

#include <d3d11.h>

#include <Asserts.h>

namespace nCine::RHI::D3D11
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

	std::uint32_t D3D11Texture::_nextHandle = 1;

	D3D11Texture::D3D11Texture(TextureTarget target)
		: _handle(_nextHandle++), _target(target), _format(PixelFormat::Unknown), _uploadFormat(PixelFormat::Unknown),
			_width(0), _height(0), _strideBytes(0),
			_minFilter(nCine::SamplerFilter::Nearest), _magFilter(nCine::SamplerFilter::Nearest), _wrap(SamplerWrapping::ClampToEdge),
			_textureUnit(0), _isRenderTarget(false),
			_gpuTexture(nullptr), _srv(nullptr), _sampler(nullptr), _contentsDirty(false), _hasCpuData(false),
			_samplerMinFilter(nCine::SamplerFilter::Nearest), _samplerFilter(nCine::SamplerFilter::Nearest), _samplerWrap(SamplerWrapping::ClampToEdge)
	{
		_swizzle[0] = SwizzleChannel::Red;
		_swizzle[1] = SwizzleChannel::Green;
		_swizzle[2] = SwizzleChannel::Blue;
		_swizzle[3] = SwizzleChannel::Alpha;
	}

	D3D11Texture::~D3D11Texture()
	{
		// Unbind from the device first so a later draw can't dereference this freed texture via _boundTextures
		// (crashed in BindTextures during splitscreen level changes)
		D3D11Device::UnbindTexture(this);
		ReleaseGpu();
	}

	void D3D11Texture::ReleaseGpu() const
	{
		if (_srv != nullptr) { _srv->Release(); _srv = nullptr; }
		if (_gpuTexture != nullptr) { _gpuTexture->Release(); _gpuTexture = nullptr; }
		if (_sampler != nullptr) { _sampler->Release(); _sampler = nullptr; }
	}

	bool D3D11Texture::IsIdentitySwizzle() const
	{
		return (_swizzle[0] == SwizzleChannel::Red && _swizzle[1] == SwizzleChannel::Green &&
			_swizzle[2] == SwizzleChannel::Blue && _swizzle[3] == SwizzleChannel::Alpha);
	}

	const std::uint8_t* D3D11Texture::SwizzledUploadPixels() const
	{
		// The store is always RGBA8 (4 bpp) after Allocate()'s promotion. D3D11's base SRV cannot remap
		// channels the way GL's GL_TEXTURE_SWIZZLE_* does, so a non-identity swizzle (e.g. the palette-index
		// RG8 textures set A<-Green so the shader's `src.a` reads the packed alpha byte) is baked into the
		// uploaded texels here. Without this, `src.a` would always be 1.0 and RG8 sprites (gems, pre-packed
		// index+alpha) would lose their transparency; R8 textures keep the identity swizzle and are untouched.
		if (_pixels.empty() || IsIdentitySwizzle()) {
			return _pixels.data();
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
		_swizzledPixels.resize(_pixels.size());
		const std::size_t texelCount = _pixels.size() / 4;
		for (std::size_t i = 0; i < texelCount; i++) {
			const std::uint8_t* in = &_pixels[i * 4];
			std::uint8_t* out = &_swizzledPixels[i * 4];
			out[0] = pick(_swizzle[0], in);
			out[1] = pick(_swizzle[1], in);
			out[2] = pick(_swizzle[2], in);
			out[3] = pick(_swizzle[3], in);
		}
		return _swizzledPixels.data();
	}

	void D3D11Texture::EnsureGpuTexture() const
	{
		ID3D11Device* device = D3D11Device::GetD3DDevice();
		if (device == nullptr || _width <= 0 || _height <= 0) {
			return;
		}

		if (_gpuTexture != nullptr && !_contentsDirty) {
			return;
		}

		// A CPU re-upload into an existing same-size texture just refreshes its contents
		if (_gpuTexture != nullptr && _contentsDirty && !_isRenderTarget && _hasCpuData && !_pixels.empty()) {
			ID3D11DeviceContext* context = D3D11Device::GetD3DContext();
			if (context != nullptr) {
				context->UpdateSubresource(_gpuTexture, 0, nullptr, SwizzledUploadPixels(), std::uint32_t(_strideBytes), 0);
				_contentsDirty = false;
				return;
			}
		}

		// (Re)create the texture from scratch. The host store is always RGBA8 after the promotion in
		// Allocate(), so a single DXGI format covers every runtime texture.
		if (_srv != nullptr) { _srv->Release(); _srv = nullptr; }
		if (_gpuTexture != nullptr) { _gpuTexture->Release(); _gpuTexture = nullptr; }

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = static_cast<UINT>(_width);
		desc.Height = static_cast<UINT>(_height);
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (_isRenderTarget ? D3D11_BIND_RENDER_TARGET : 0u);
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;

		D3D11_SUBRESOURCE_DATA init = {};
		const bool hasInit = (_hasCpuData && !_isRenderTarget && !_pixels.empty());
		if (hasInit) {
			init.pSysMem = SwizzledUploadPixels();
			init.SysMemPitch = static_cast<UINT>(_strideBytes);
		}

		if (FAILED(device->CreateTexture2D(&desc, hasInit ? &init : nullptr, &_gpuTexture))) {
			_gpuTexture = nullptr;
			return;
		}
		device->CreateShaderResourceView(_gpuTexture, nullptr, &_srv);
		_contentsDirty = false;
	}

	ID3D11ShaderResourceView* D3D11Texture::GetSRV() const
	{
		EnsureGpuTexture();
		return _srv;
	}

	ID3D11Texture2D* D3D11Texture::GetOrCreateTexture2D() const
	{
		EnsureGpuTexture();
		return _gpuTexture;
	}

	ID3D11SamplerState* D3D11Texture::GetSampler() const
	{
		ID3D11Device* device = D3D11Device::GetD3DDevice();
		if (device == nullptr) {
			return nullptr;
		}
		if (_sampler != nullptr && _samplerFilter == _magFilter && _samplerMinFilter == _minFilter && _samplerWrap == _wrap) {
			return _sampler;
		}
		if (_sampler != nullptr) { _sampler->Release(); _sampler = nullptr; }

		D3D11_SAMPLER_DESC desc = {};
		// Compose the filter from both the minification and magnification modes (mip mode is irrelevant, the
		// backend only ever stores level 0)
		const bool magLinear = (_magFilter == nCine::SamplerFilter::Linear ||
			_magFilter == nCine::SamplerFilter::LinearMipmapNearest || _magFilter == nCine::SamplerFilter::LinearMipmapLinear);
		const bool minLinear = (_minFilter == nCine::SamplerFilter::Linear ||
			_minFilter == nCine::SamplerFilter::LinearMipmapNearest || _minFilter == nCine::SamplerFilter::LinearMipmapLinear);
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
		switch (_wrap) {
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
		device->CreateSamplerState(&desc, &_sampler);
		_samplerMinFilter = _minFilter;
		_samplerFilter = _magFilter;
		_samplerWrap = _wrap;
		return _sampler;
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
		// original in _uploadFormat. Mirrors the software backend's promotion so the backend can lift the same
		// bytes into an `ID3D11Texture2D` without a separate widening step.
		_uploadFormat = format;
		_format = (format == PixelFormat::RGB8 || format == PixelFormat::R8 || format == PixelFormat::RG8) ? PixelFormat::RGBA8 : format;
		_width = width;
		_height = height;
		const std::int32_t bpp = BytesPerPixel(_format);
		_strideBytes = width * bpp;
		_pixels.assign(std::size_t(_strideBytes) * std::size_t(height > 0 ? height : 0), std::uint8_t(0));
		// The size/format changed, so any existing GPU texture must be rebuilt on the next bind
		if (_srv != nullptr) { _srv->Release(); _srv = nullptr; }
		if (_gpuTexture != nullptr) { _gpuTexture->Release(); _gpuTexture = nullptr; }
		_contentsDirty = true;
	}

	bool D3D11Texture::Bind(std::uint32_t textureUnit) const
	{
		_textureUnit = textureUnit;
		D3D11Device::BindTexture(textureUnit, this);
		return true;
	}

	bool D3D11Texture::Unbind() const
	{
		D3D11Device::BindTexture(_textureUnit, nullptr);
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
		if (data != nullptr && !_pixels.empty()) {
			_hasCpuData = true;
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

	void D3D11Texture::TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data)
	{
		static_cast<void>(bgr);
		if (level != 0 || data == nullptr || _pixels.empty()) {
			return;
		}
		_hasCpuData = true;
		_contentsDirty = true;
		const std::int32_t srcBpp = BytesPerPixel(format);
		const std::int32_t dstBpp = BytesPerPixel(_format);
		for (std::int32_t y = 0; y < height; y++) {
			const std::int32_t dstY = yoffset + y;
			if (dstY < 0 || dstY >= _height) {
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
		if (_isRenderTarget && _gpuTexture != nullptr) {
			ID3D11Device* device = D3D11Device::GetD3DDevice();
			ID3D11DeviceContext* context = D3D11Device::GetD3DContext();
			if (device != nullptr && context != nullptr) {
				D3D11_TEXTURE2D_DESC desc = {};
				_gpuTexture->GetDesc(&desc);
				desc.Usage = D3D11_USAGE_STAGING;
				desc.BindFlags = 0;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				desc.MiscFlags = 0;
				ID3D11Texture2D* staging = nullptr;
				if (SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging))) {
					context->CopyResource(staging, _gpuTexture);
					D3D11_MAPPED_SUBRESOURCE mapped;
					if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
						const std::uint32_t rowBytes = std::uint32_t(_width) * 4;
						std::uint8_t* dst = static_cast<std::uint8_t*>(pixels);
						const std::uint8_t* src = static_cast<const std::uint8_t*>(mapped.pData);
						for (std::int32_t y = 0; y < _height; y++) {
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

		if (!_pixels.empty()) {
			std::memcpy(pixels, _pixels.data(), _pixels.size());
		}
	}

	void D3D11Texture::SetMinFiltering(nCine::SamplerFilter filter)
	{
		_minFilter = filter;
	}

	void D3D11Texture::SetMagFiltering(nCine::SamplerFilter filter)
	{
		_magFilter = filter;
	}

	void D3D11Texture::SetWrap(SamplerWrapping wrap)
	{
		_wrap = wrap;
	}

	void D3D11Texture::SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a)
	{
		_swizzle[0] = r;
		_swizzle[1] = g;
		_swizzle[2] = b;
		_swizzle[3] = a;
		// The swizzle is baked into the uploaded texels (D3D11 has no SRV channel remap), so a change must
		// rebuild the GPU texture; it is usually set right after the upload, before the first bind.
		_contentsDirty = true;
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
