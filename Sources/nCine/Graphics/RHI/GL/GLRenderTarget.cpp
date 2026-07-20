#include "GLRenderTarget.h"

#include <Containers/StringView.h>

namespace nCine::RHI::GL
{
	namespace
	{
		GLenum DepthStencilFormatToGLFormat(DepthStencilFormat format)
		{
#if defined(RHI_GL_PROFILE_ES2)
			// The only depth renderbuffer format guaranteed by ES 2.0 core is DEPTH_COMPONENT16 (24-bit depth
			// needs OES_depth24, packed depth-stencil needs OES_packed_depth_stencil - neither is assumed).
			// The game's texture render targets all use DepthStencilFormat::None, so this only guards spec
			// correctness for potential future users
			static_cast<void>(format);
			return GL_DEPTH_COMPONENT16;
#else
			switch (format) {
				case DepthStencilFormat::Depth16:
					return GL_DEPTH_COMPONENT16;
				case DepthStencilFormat::Depth24:
					return GL_DEPTH_COMPONENT24;
				default:
				case DepthStencilFormat::Depth24_Stencil8:
					return GL_DEPTH24_STENCIL8;
			}
#endif
		}

		GLenum DepthStencilFormatToGLAttachment(DepthStencilFormat format)
		{
#if defined(RHI_GL_PROFILE_ES2)
			// ES2 has no GL_DEPTH_STENCIL_ATTACHMENT; everything attaches as a plain depth buffer here
			// (matches DepthStencilFormatToGLFormat() above, which always yields DEPTH_COMPONENT16)
			static_cast<void>(format);
			return GL_DEPTH_ATTACHMENT;
#else
			switch (format) {
				case DepthStencilFormat::Depth16:
				case DepthStencilFormat::Depth24:
					return GL_DEPTH_ATTACHMENT;
				default:
				case DepthStencilFormat::Depth24_Stencil8:
					return GL_DEPTH_STENCIL_ATTACHMENT;
			}
#endif
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
