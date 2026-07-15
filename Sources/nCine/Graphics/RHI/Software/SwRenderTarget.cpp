#include "SwRenderTarget.h"
#include "SwDevice.h"
#include "SwTexture.h"

namespace nCine::RhiSoftware
{
	SwRenderTarget::SwRenderTarget()
		: numDrawBuffers_(1)
	{
		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			colorTextures_[i] = nullptr;
		}
	}

	void SwRenderTarget::AttachColorTexture(SwTexture& texture, std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			colorTextures_[index] = &texture;
			// Tag the texture so the rasterizer treats its store as bottom-up (GL framebuffer convention)
			// both when drawing into it and when later sampling it as a source
			texture.SetRenderTarget(true);
		}
	}

	void SwRenderTarget::DetachColorTexture(std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			colorTextures_[index] = nullptr;
		}
	}

	void SwRenderTarget::AttachDepthStencil(DepthStencilFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(format);
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void SwRenderTarget::DetachDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void SwRenderTarget::BindDraw()
	{
		SwDevice::SetRenderTarget(this);
	}

	void SwRenderTarget::UnbindDraw()
	{
		SwDevice::SetRenderTarget(nullptr);
	}

	bool SwRenderTarget::SetDrawBuffers(std::uint32_t numColorAttachments)
	{
		numDrawBuffers_ = numColorAttachments;
		return true;
	}

	bool SwRenderTarget::IsStatusComplete()
	{
		return (colorTextures_[0] != nullptr);
	}

	void SwRenderTarget::InvalidateDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void SwRenderTarget::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
