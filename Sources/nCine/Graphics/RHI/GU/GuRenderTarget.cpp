#include "GuRenderTarget.h"
#include "GuDevice.h"
#include "GuTexture.h"

namespace nCine::RHI::GU
{
	GuRenderTarget::GuRenderTarget()
		: numDrawBuffers_(1)
	{
		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			colorTextures_[i] = nullptr;
		}
	}

	GuRenderTarget::~GuRenderTarget()
	{
		// Clear from the device so a destroyed target can't dangle as currentRenderTarget_
		GuDevice::UnbindRenderTarget(this);
	}

	void GuRenderTarget::AttachColorTexture(GuTexture& texture, std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			colorTextures_[index] = &texture;
			// Tag the texture so the rasterizer treats its store as bottom-up (GL framebuffer convention)
			// both when drawing into it and when later sampling it as a source
			texture.SetRenderTarget(true);
		}
	}

	void GuRenderTarget::DetachColorTexture(std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			colorTextures_[index] = nullptr;
		}
	}

	void GuRenderTarget::AttachDepthStencil(DepthStencilFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(format);
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void GuRenderTarget::DetachDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void GuRenderTarget::BindDraw()
	{
		GuDevice::SetRenderTarget(this);
	}

	void GuRenderTarget::UnbindDraw()
	{
		GuDevice::SetRenderTarget(nullptr);
	}

	bool GuRenderTarget::SetDrawBuffers(std::uint32_t numColorAttachments)
	{
		numDrawBuffers_ = numColorAttachments;
		return true;
	}

	bool GuRenderTarget::IsStatusComplete()
	{
		return (colorTextures_[0] != nullptr);
	}

	void GuRenderTarget::InvalidateDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void GuRenderTarget::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
