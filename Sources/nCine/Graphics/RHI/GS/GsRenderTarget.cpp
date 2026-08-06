#include "GsRenderTarget.h"
#include "GsDevice.h"
#include "GsTexture.h"

namespace nCine::RHI::GS
{
	GsRenderTarget::GsRenderTarget()
		: _numDrawBuffers(1)
	{
		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			_colorTextures[i] = nullptr;
		}
	}

	GsRenderTarget::~GsRenderTarget()
	{
		// Clear from the device so a destroyed target can't dangle as the current one
		GsDevice::UnbindRenderTarget(this);
	}

	void GsRenderTarget::AttachColorTexture(GsTexture& texture, std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			_colorTextures[index] = &texture;
			// Becoming a render target allocates the texture's surface out of the reserve and marks it
			// non-evictable; it also tags the store as bottom-up (the GL framebuffer convention) both for
			// drawing into it and for later sampling it as a source
			texture.SetRenderTarget(true);
		}
	}

	void GsRenderTarget::DetachColorTexture(std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			_colorTextures[index] = nullptr;
		}
	}

	void GsRenderTarget::AttachDepthStencil(DepthStencilFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(format);
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void GsRenderTarget::DetachDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void GsRenderTarget::BindDraw()
	{
		GsDevice::SetRenderTarget(this);
	}

	void GsRenderTarget::UnbindDraw()
	{
		GsDevice::SetRenderTarget(nullptr);
	}

	bool GsRenderTarget::SetDrawBuffers(std::uint32_t numColorAttachments)
	{
		_numDrawBuffers = numColorAttachments;
		return true;
	}

	bool GsRenderTarget::IsStatusComplete()
	{
		return (_colorTextures[0] != nullptr);
	}

	void GsRenderTarget::InvalidateDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void GsRenderTarget::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
