#include "LegacyGlRenderTarget.h"
#include "LegacyGlApi.h"
#include "LegacyGlDevice.h"
#include "LegacyGlTexture.h"
#include "../../../../Main.h"

namespace nCine::RHI::LegacyGL
{
	LegacyGlRenderTarget::LegacyGlRenderTarget()
		: _numDrawBuffers(1), _framebuffer(0)
	{
		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			_colorTextures[i] = nullptr;
		}
	}

	LegacyGlRenderTarget::~LegacyGlRenderTarget()
	{
		// Clear from the device so a destroyed target can't dangle as _currentRenderTarget
		LegacyGlDevice::UnbindRenderTarget(this);
#if defined(RHI_LEGACYGL_HAS_FBO)
		if (_framebuffer != 0) {
			GLuint name = _framebuffer;
			glDeleteFramebuffers(1, &name);
			_framebuffer = 0;
		}
#endif
	}

	void LegacyGlRenderTarget::AttachColorTexture(LegacyGlTexture& texture, std::uint32_t index)
	{
		if (index >= MaxColorAttachments) {
			return;
		}
		_colorTextures[index] = &texture;
		// Becoming a render target replaces the texture's page with one GL renders into
		texture.SetRenderTarget(true);
		if (index == 0) {
			UpdateFramebuffer();
		}
	}

	void LegacyGlRenderTarget::DetachColorTexture(std::uint32_t index)
	{
		if (index >= MaxColorAttachments) {
			return;
		}
		_colorTextures[index] = nullptr;
		if (index == 0) {
			UpdateFramebuffer();
		}
	}

	void LegacyGlRenderTarget::UpdateFramebuffer()
	{
#if defined(RHI_LEGACYGL_HAS_FBO)
		if (!LegacyGlDevice::SupportsFramebufferObjects()) {
			return;
		}
		const LegacyGlTexture* texture = _colorTextures[0];
		const std::uint32_t attachment = (texture != nullptr ? texture->GetRenderTargetTexture() : 0);
		if (attachment == 0) {
			// Nothing to point at; the target stays incomplete until a usable texture is attached
			return;
		}
		if (_framebuffer == 0) {
			GLuint name = 0;
			glGenFramebuffers(1, &name);
			if (name == 0) {
				LOGW("Cannot create a framebuffer object, the render target falls back to copying");
				return;
			}
			_framebuffer = name;
		}
		glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, attachment, 0);
		const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		LegacyGlDevice::InvalidateStateCache();
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			LOGW("Framebuffer object is incomplete (0x{:.4x}), the render target falls back to copying", std::uint32_t(status));
			GLuint name = _framebuffer;
			glDeleteFramebuffers(1, &name);
			_framebuffer = 0;
		}
#endif
	}

	void LegacyGlRenderTarget::ResolveCopyBack() const
	{
		const LegacyGlTexture* texture = _colorTextures[0];
		if (_framebuffer != 0 || texture == nullptr) {
			return;
		}
		const std::uint32_t name = texture->GetRenderTargetTexture();
		if (name == 0) {
			return;
		}
		// The pass rendered into the bottom-left corner of the back buffer, in the target's own pixels
		// (see LegacyGlDevice::ApplyDrawTarget), so the copy is one region starting at the origin
		std::int32_t width = texture->GetWidth(), height = texture->GetHeight();
		LegacyGlDevice::ClampToDrawable(width, height);
		if (width <= 0 || height <= 0) {
			return;
		}
		glBindTexture(GL_TEXTURE_2D, name);
		glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
		LegacyGlDevice::InvalidateStateCache();
	}

	void LegacyGlRenderTarget::AttachDepthStencil(DepthStencilFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(format);
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void LegacyGlRenderTarget::DetachDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void LegacyGlRenderTarget::BindDraw()
	{
		LegacyGlDevice::SetRenderTarget(this);
	}

	void LegacyGlRenderTarget::UnbindDraw()
	{
		LegacyGlDevice::SetRenderTarget(nullptr);
	}

	bool LegacyGlRenderTarget::SetDrawBuffers(std::uint32_t numColorAttachments)
	{
		_numDrawBuffers = numColorAttachments;
		return true;
	}

	bool LegacyGlRenderTarget::IsStatusComplete()
	{
		return (_colorTextures[0] != nullptr && _colorTextures[0]->GetRenderTargetTexture() != 0);
	}

	void LegacyGlRenderTarget::InvalidateDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void LegacyGlRenderTarget::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
