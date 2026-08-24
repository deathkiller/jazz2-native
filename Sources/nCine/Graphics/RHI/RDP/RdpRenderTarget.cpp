#include "RdpRenderTarget.h"
#include "RdpDevice.h"
#include "RdpTexture.h"

namespace nCine::RHI::RDP
{
	RdpRenderTarget::RdpRenderTarget()
		: _numDrawBuffers(1)
	{
		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			_colorTextures[i] = nullptr;
		}
	}

	RdpRenderTarget::~RdpRenderTarget()
	{
		// Clear from the device so a destroyed target can't dangle as _currentRenderTarget
		RdpDevice::UnbindRenderTarget(this);
	}

	void RdpRenderTarget::AttachColorTexture(RdpTexture& texture, std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			_colorTextures[index] = &texture;
			// Tag the texture so the rasterizer treats its store as bottom-up (GL framebuffer convention)
			// both when drawing into it and when later sampling it as a source
			texture.SetRenderTarget(true);
		}
	}

	void RdpRenderTarget::DetachColorTexture(std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			_colorTextures[index] = nullptr;
		}
	}

	void RdpRenderTarget::AttachDepthStencil(DepthStencilFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(format);
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void RdpRenderTarget::DetachDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void RdpRenderTarget::BindDraw()
	{
		RdpDevice::SetRenderTarget(this);
	}

	void RdpRenderTarget::UnbindDraw()
	{
		RdpDevice::SetRenderTarget(nullptr);
	}

	bool RdpRenderTarget::SetDrawBuffers(std::uint32_t numColorAttachments)
	{
		_numDrawBuffers = numColorAttachments;
		return true;
	}

	bool RdpRenderTarget::IsStatusComplete()
	{
		return (_colorTextures[0] != nullptr);
	}

	void RdpRenderTarget::InvalidateDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void RdpRenderTarget::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
