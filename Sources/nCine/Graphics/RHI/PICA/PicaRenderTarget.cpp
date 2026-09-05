#include "PicaRenderTarget.h"
#include "PicaDevice.h"
#include "PicaTexture.h"

namespace nCine::RHI::PICA
{
	PicaRenderTarget::PicaRenderTarget()
		: _numDrawBuffers(1)
	{
		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			_colorTextures[i] = nullptr;
		}
	}

	PicaRenderTarget::~PicaRenderTarget()
	{
		// Clear from the device so a destroyed target can't dangle as _currentRenderTarget
		PicaDevice::UnbindRenderTarget(this);
	}

	void PicaRenderTarget::AttachColorTexture(PicaTexture& texture, std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			_colorTextures[index] = &texture;
			// Tag the texture so the rasterizer treats its store as bottom-up (GL framebuffer convention)
			// both when drawing into it and when later sampling it as a source
			texture.SetRenderTarget(true);
		}
	}

	void PicaRenderTarget::DetachColorTexture(std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			_colorTextures[index] = nullptr;
		}
	}

	void PicaRenderTarget::AttachDepthStencil(DepthStencilFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(format);
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void PicaRenderTarget::DetachDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void PicaRenderTarget::BindDraw()
	{
		PicaDevice::SetRenderTarget(this);
	}

	void PicaRenderTarget::UnbindDraw()
	{
		PicaDevice::SetRenderTarget(nullptr);
	}

	bool PicaRenderTarget::SetDrawBuffers(std::uint32_t numColorAttachments)
	{
		_numDrawBuffers = numColorAttachments;
		return true;
	}

	bool PicaRenderTarget::IsStatusComplete()
	{
		return (_colorTextures[0] != nullptr);
	}

	void PicaRenderTarget::InvalidateDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void PicaRenderTarget::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
