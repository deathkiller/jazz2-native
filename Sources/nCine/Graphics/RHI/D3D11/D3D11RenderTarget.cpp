#include "D3D11RenderTarget.h"
#include "D3D11Device.h"
#include "D3D11Texture.h"

#include <d3d11.h>

namespace nCine::RhiD3D11
{
	D3D11RenderTarget::D3D11RenderTarget()
		: numDrawBuffers_(1), rtv_(nullptr), rtvTexture_(nullptr)
	{
		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			colorTextures_[i] = nullptr;
		}
	}

	D3D11RenderTarget::~D3D11RenderTarget()
	{
		// Clear from the device first so a destroyed render target can't dangle as currentRenderTarget_
		D3D11Device::UnbindRenderTarget(this);
		if (rtv_ != nullptr) {
			rtv_->Release();
			rtv_ = nullptr;
		}
	}

	ID3D11RenderTargetView* D3D11RenderTarget::GetRTV()
	{
		D3D11Texture* color = colorTextures_[0];
		if (color == nullptr) {
			return nullptr;
		}
		// Rebuild if the attachment changed or the view has not been created yet
		if (rtv_ != nullptr && rtvTexture_ == color) {
			return rtv_;
		}
		if (rtv_ != nullptr) {
			rtv_->Release();
			rtv_ = nullptr;
		}

		ID3D11Device* device = D3D11Device::GetD3DDevice();
		ID3D11Texture2D* tex = color->GetOrCreateTexture2D();
		if (device == nullptr || tex == nullptr) {
			return nullptr;
		}
		if (FAILED(device->CreateRenderTargetView(tex, nullptr, &rtv_))) {
			rtv_ = nullptr;
			return nullptr;
		}
		rtvTexture_ = color;
		return rtv_;
	}

	void D3D11RenderTarget::AttachColorTexture(D3D11Texture& texture, std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			colorTextures_[index] = &texture;
			texture.SetRenderTarget(true);
			if (index == 0 && rtv_ != nullptr) {
				rtv_->Release();
				rtv_ = nullptr;
				rtvTexture_ = nullptr;
			}
		}
	}

	void D3D11RenderTarget::DetachColorTexture(std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			colorTextures_[index] = nullptr;
			if (index == 0 && rtv_ != nullptr) {
				rtv_->Release();
				rtv_ = nullptr;
				rtvTexture_ = nullptr;
			}
		}
	}

	void D3D11RenderTarget::AttachDepthStencil(DepthStencilFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(format);
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void D3D11RenderTarget::DetachDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void D3D11RenderTarget::BindDraw()
	{
		D3D11Device::SetRenderTarget(this);
	}

	void D3D11RenderTarget::UnbindDraw()
	{
		D3D11Device::SetRenderTarget(nullptr);
	}

	bool D3D11RenderTarget::SetDrawBuffers(std::uint32_t numColorAttachments)
	{
		numDrawBuffers_ = numColorAttachments;
		return true;
	}

	bool D3D11RenderTarget::IsStatusComplete()
	{
		return (colorTextures_[0] != nullptr);
	}

	void D3D11RenderTarget::InvalidateDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void D3D11RenderTarget::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
