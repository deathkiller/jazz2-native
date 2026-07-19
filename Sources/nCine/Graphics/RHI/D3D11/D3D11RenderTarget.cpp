#include "D3D11RenderTarget.h"
#include "D3D11Device.h"
#include "D3D11Texture.h"

#include <d3d11.h>

namespace nCine::RhiD3D11
{
	D3D11RenderTarget::D3D11RenderTarget()
		: numDrawBuffers_(1)
	{
		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			colorTextures_[i] = nullptr;
			rtvs_[i] = nullptr;
			rtvTextures_[i] = nullptr;
		}
	}

	D3D11RenderTarget::~D3D11RenderTarget()
	{
		// Clear from the device first so a destroyed render target can't dangle as currentRenderTarget_
		// (this also scrubs every released RTV from the device's last-bound shadow)
		D3D11Device::UnbindRenderTarget(this);
		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			ReleaseRTV(i);
		}
	}

	void D3D11RenderTarget::ReleaseRTV(std::uint32_t index)
	{
		if (rtvs_[index] != nullptr) {
			// Scrub from the device's last-bound shadow first: the view is released right after this call and a
			// new RTV recycling the same pointer would otherwise be mistaken for still bound and its bind skipped
			D3D11Device::OnRtvReleased(rtvs_[index]);
			rtvs_[index]->Release();
			rtvs_[index] = nullptr;
		}
		rtvTextures_[index] = nullptr;
	}

	ID3D11RenderTargetView* D3D11RenderTarget::GetRTV(std::uint32_t index)
	{
		if (index >= MaxColorAttachments) {
			return nullptr;
		}
		D3D11Texture* color = colorTextures_[index];
		if (color == nullptr) {
			return nullptr;
		}
		// Rebuild if the attachment changed or the view has not been created yet
		if (rtvs_[index] != nullptr && rtvTextures_[index] == color) {
			return rtvs_[index];
		}
		ReleaseRTV(index);

		ID3D11Device* device = D3D11Device::GetD3DDevice();
		ID3D11Texture2D* tex = color->GetOrCreateTexture2D();
		if (device == nullptr || tex == nullptr) {
			return nullptr;
		}
		if (FAILED(device->CreateRenderTargetView(tex, nullptr, &rtvs_[index]))) {
			rtvs_[index] = nullptr;
			return nullptr;
		}
		rtvTextures_[index] = color;
		return rtvs_[index];
	}

	std::uint32_t D3D11RenderTarget::GetAttachedCount() const
	{
		std::uint32_t count = 0;
		while (count < MaxColorAttachments && colorTextures_[count] != nullptr) {
			count++;
		}
		return count;
	}

	std::uint32_t D3D11RenderTarget::GetRTVs(ID3D11RenderTargetView* outRtvs[MaxColorAttachments])
	{
		// The bound set is the contiguous attached run, limited by the draw-buffer count (mirroring
		// glDrawBuffers) and truncated at the first attachment whose view cannot be created
		std::uint32_t count = GetAttachedCount();
		if (numDrawBuffers_ > 0 && numDrawBuffers_ < count) {
			count = numDrawBuffers_;
		}
		for (std::uint32_t i = 0; i < count; i++) {
			outRtvs[i] = GetRTV(i);
			if (outRtvs[i] == nullptr) {
				return i;
			}
		}
		return count;
	}

	void D3D11RenderTarget::AttachColorTexture(D3D11Texture& texture, std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			colorTextures_[index] = &texture;
			texture.SetRenderTarget(true);
			ReleaseRTV(index);
		}
	}

	void D3D11RenderTarget::DetachColorTexture(std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			colorTextures_[index] = nullptr;
			ReleaseRTV(index);
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
