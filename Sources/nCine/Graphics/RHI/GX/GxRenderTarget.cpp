#include "GxRenderTarget.h"
#include "GxDevice.h"
#include "GxTexture.h"

namespace nCine::RHI::GX
{
	GxRenderTarget::GxRenderTarget()
		: _numDrawBuffers(1)
	{
		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			_colorTextures[i] = nullptr;
		}
	}

	GxRenderTarget::~GxRenderTarget()
	{
		// Clear from the device so a destroyed target can't dangle as _currentRenderTarget
		GxDevice::UnbindRenderTarget(this);
	}

	void GxRenderTarget::AttachColorTexture(GxTexture& texture, std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			_colorTextures[index] = &texture;
			// Tag the texture so the rasterizer treats its store as bottom-up (GL framebuffer convention)
			// both when drawing into it and when later sampling it as a source
			texture.SetRenderTarget(true);
		}
	}

	void GxRenderTarget::DetachColorTexture(std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			_colorTextures[index] = nullptr;
		}
	}

	void GxRenderTarget::AttachDepthStencil(DepthStencilFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(format);
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void GxRenderTarget::DetachDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void GxRenderTarget::BindDraw()
	{
		GxDevice::SetRenderTarget(this);
	}

	void GxRenderTarget::UnbindDraw()
	{
		GxDevice::SetRenderTarget(nullptr);
	}

	bool GxRenderTarget::SetDrawBuffers(std::uint32_t numColorAttachments)
	{
		_numDrawBuffers = numColorAttachments;
		return true;
	}

	bool GxRenderTarget::IsStatusComplete()
	{
		return (_colorTextures[0] != nullptr);
	}

	void GxRenderTarget::InvalidateDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void GxRenderTarget::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
