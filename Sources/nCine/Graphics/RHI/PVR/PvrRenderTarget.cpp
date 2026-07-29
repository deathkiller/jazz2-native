#include "PvrRenderTarget.h"
#include "PvrDevice.h"
#include "PvrTexture.h"

namespace nCine::RHI::PVR
{
	PvrRenderTarget::PvrRenderTarget()
		: numDrawBuffers_(1)
	{
		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			colorTextures_[i] = nullptr;
		}
	}

	PvrRenderTarget::~PvrRenderTarget()
	{
		// Clear from the device so a destroyed target can't dangle as currentRenderTarget_
		PvrDevice::UnbindRenderTarget(this);
	}

	void PvrRenderTarget::AttachColorTexture(PvrTexture& texture, std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			colorTextures_[index] = &texture;
			// Tag the texture so the rasterizer treats its store as bottom-up (GL framebuffer convention)
			// both when drawing into it and when later sampling it as a source
			texture.SetRenderTarget(true);
		}
	}

	void PvrRenderTarget::DetachColorTexture(std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			colorTextures_[index] = nullptr;
		}
	}

	void PvrRenderTarget::AttachDepthStencil(DepthStencilFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(format);
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void PvrRenderTarget::DetachDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void PvrRenderTarget::BindDraw()
	{
		PvrDevice::SetRenderTarget(this);
	}

	void PvrRenderTarget::UnbindDraw()
	{
		PvrDevice::SetRenderTarget(nullptr);
	}

	bool PvrRenderTarget::SetDrawBuffers(std::uint32_t numColorAttachments)
	{
		numDrawBuffers_ = numColorAttachments;
		return true;
	}

	bool PvrRenderTarget::IsStatusComplete()
	{
		return (colorTextures_[0] != nullptr);
	}

	void PvrRenderTarget::InvalidateDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void PvrRenderTarget::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
