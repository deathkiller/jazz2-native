#include "PvrRenderTarget.h"
#include "PvrDevice.h"
#include "PvrTexture.h"

namespace nCine::RHI::PVR
{
	PvrRenderTarget::PvrRenderTarget()
		: _numDrawBuffers(1)
	{
		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			_colorTextures[i] = nullptr;
		}
	}

	PvrRenderTarget::~PvrRenderTarget()
	{
		// Clear from the device so a destroyed target can't dangle as _currentRenderTarget
		PvrDevice::UnbindRenderTarget(this);
	}

	void PvrRenderTarget::AttachColorTexture(PvrTexture& texture, std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			_colorTextures[index] = &texture;
			// Tag the texture so the rasterizer treats its store as bottom-up (GL framebuffer convention)
			// both when drawing into it and when later sampling it as a source
			texture.SetRenderTarget(true);
		}
	}

	void PvrRenderTarget::DetachColorTexture(std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			_colorTextures[index] = nullptr;
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
		_numDrawBuffers = numColorAttachments;
		return true;
	}

	bool PvrRenderTarget::IsStatusComplete()
	{
		return (_colorTextures[0] != nullptr);
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
