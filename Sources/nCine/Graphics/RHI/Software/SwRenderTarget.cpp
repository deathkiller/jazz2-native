#include "SwRenderTarget.h"
#include "SwDevice.h"
#include "SwTexture.h"

namespace nCine::RHI::Software
{
	SwRenderTarget::SwRenderTarget()
		: _numDrawBuffers(1)
	{
		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			_colorTextures[i] = nullptr;
		}
	}

	SwRenderTarget::~SwRenderTarget()
	{
		// Clear from the device so a destroyed target can't dangle as _currentRenderTarget
		SwDevice::UnbindRenderTarget(this);
	}

	void SwRenderTarget::AttachColorTexture(SwTexture& texture, std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			_colorTextures[index] = &texture;
			// Tag the texture as a render target, which gives it the surface the device renders into; it is stored
			// top-down like every other texture (see RhiFwd.h), so it is sampled the same way too
			texture.SetRenderTarget(true);
		}
	}

	void SwRenderTarget::DetachColorTexture(std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			_colorTextures[index] = nullptr;
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
		_numDrawBuffers = numColorAttachments;
		return true;
	}

	bool SwRenderTarget::IsStatusComplete()
	{
		return (_colorTextures[0] != nullptr);
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
