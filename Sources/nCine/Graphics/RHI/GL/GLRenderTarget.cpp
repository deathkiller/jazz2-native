#include "GLRenderTarget.h"

#include <Containers/StringView.h>

namespace nCine::RhiGL
{
	namespace
	{
		GLenum DepthStencilFormatToGLFormat(DepthStencilFormat format)
		{
			switch (format) {
				case DepthStencilFormat::Depth16:
					return GL_DEPTH_COMPONENT16;
				case DepthStencilFormat::Depth24:
					return GL_DEPTH_COMPONENT24;
				default:
				case DepthStencilFormat::Depth24_Stencil8:
					return GL_DEPTH24_STENCIL8;
			}
		}

		GLenum DepthStencilFormatToGLAttachment(DepthStencilFormat format)
		{
			switch (format) {
				case DepthStencilFormat::Depth16:
				case DepthStencilFormat::Depth24:
					return GL_DEPTH_ATTACHMENT;
				default:
				case DepthStencilFormat::Depth24_Stencil8:
					return GL_DEPTH_STENCIL_ATTACHMENT;
			}
		}
	}

	void GLRenderTarget::AttachColorTexture(GLTexture& texture, std::uint32_t index)
	{
		fbo_.AttachTexture(texture, GL_COLOR_ATTACHMENT0 + index);
	}

	void GLRenderTarget::DetachColorTexture(std::uint32_t index)
	{
		fbo_.DetachTexture(GL_COLOR_ATTACHMENT0 + index);
	}

	void GLRenderTarget::AttachDepthStencil(DepthStencilFormat format, std::int32_t width, std::int32_t height)
	{
		fbo_.AttachRenderbuffer(DepthStencilFormatToGLFormat(format), width, height, DepthStencilFormatToGLAttachment(format));
	}

	void GLRenderTarget::DetachDepthStencil(DepthStencilFormat format)
	{
		fbo_.DetachRenderbuffer(DepthStencilFormatToGLAttachment(format));
	}

	void GLRenderTarget::BindDraw()
	{
		fbo_.Bind(GL_DRAW_FRAMEBUFFER);
	}

	void GLRenderTarget::UnbindDraw()
	{
		GLFramebuffer::Unbind(GL_DRAW_FRAMEBUFFER);
	}

	bool GLRenderTarget::SetDrawBuffers(std::uint32_t numColorAttachments)
	{
		return fbo_.DrawBuffers(numColorAttachments);
	}

	bool GLRenderTarget::IsStatusComplete()
	{
		return fbo_.IsStatusComplete();
	}

	void GLRenderTarget::InvalidateDepthStencil(DepthStencilFormat format)
	{
#if !(defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_ARM))
		const GLenum invalidAttachment = DepthStencilFormatToGLAttachment(format);
		fbo_.Invalidate(1, &invalidAttachment);
#else
		static_cast<void>(format);
#endif
	}

	void GLRenderTarget::SetObjectLabel(StringView label)
	{
		fbo_.SetObjectLabel(label);
	}
}
