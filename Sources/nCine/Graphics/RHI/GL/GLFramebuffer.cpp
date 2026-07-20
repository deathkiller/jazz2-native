#include "GLFramebuffer.h"
#include "GLRenderbuffer.h"
#include "GLTexture.h"
#include "GLDebug.h"
#include "../../../../Main.h"

namespace nCine::RHI::GL
{
	std::uint32_t GLFramebuffer::readBoundBuffer_ = 0;
	std::uint32_t GLFramebuffer::drawBoundBuffer_ = 0;

	GLFramebuffer::GLFramebuffer()
		: glHandle_(0), numDrawBuffers_(0)
	{
		glGenFramebuffers(1, &glHandle_);
		GL_LOG_ERRORS();
	}

	GLFramebuffer::~GLFramebuffer()
	{
		if (readBoundBuffer_ == glHandle_) {
			Unbind(GL_READ_FRAMEBUFFER);
		}
		if (drawBoundBuffer_ == glHandle_) {
			Unbind(GL_DRAW_FRAMEBUFFER);
		}
		glDeleteFramebuffers(1, &glHandle_);
		GL_LOG_ERRORS();
	}

	bool GLFramebuffer::Bind() const
	{
		return Bind(GL_FRAMEBUFFER);
	}

	bool GLFramebuffer::Unbind()
	{
		return Unbind(GL_FRAMEBUFFER);
	}

	bool GLFramebuffer::Bind(GLenum target) const
	{
		return BindHandle(target, glHandle_);
	}

	bool GLFramebuffer::Unbind(GLenum target)
	{
		return BindHandle(target, 0);
	}

	bool GLFramebuffer::DrawBuffers(std::uint32_t numDrawBuffers)
	{
#if defined(RHI_GL_PROFILE_ES2)
		// glDrawBuffers() is ES 3.0 and MRT does not exist on ES2 - a framebuffer always renders to its single
		// GL_COLOR_ATTACHMENT0, which is exactly what a draw-buffer count of 0/1 selects, so those are no-ops
		DEATH_ASSERT(numDrawBuffers <= 1, "Multiple render targets are not supported on OpenGL|ES 2.0", false);
		numDrawBuffers_ = numDrawBuffers;
		return false;
#else
		static const GLenum drawBuffers[MaxDrawbuffers] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3,
															GL_COLOR_ATTACHMENT4, GL_COLOR_ATTACHMENT5, GL_COLOR_ATTACHMENT6, GL_COLOR_ATTACHMENT7 };
		if (numDrawBuffers < MaxDrawbuffers && numDrawBuffers_ != numDrawBuffers) {
			glDrawBuffers(numDrawBuffers, drawBuffers);
			GL_LOG_ERRORS();
			numDrawBuffers_ = numDrawBuffers;
			return true;
		}
		return false;
#endif
	}

	bool GLFramebuffer::AttachRenderbuffer(const char* label, GLenum internalFormat, GLsizei width, GLsizei height, GLenum attachment)
	{
		if (attachedRenderbuffers_.size() >= MaxRenderbuffers - 1) {
			return false;
		}
		for (std::uint32_t i = 0; i < attachedRenderbuffers_.size(); i++) {
			if (attachedRenderbuffers_[i]->GetAttachment() == attachment) {
				return false;
			}
		}

		std::unique_ptr<GLRenderbuffer>& buffer = attachedRenderbuffers_.emplace_back(std::make_unique<GLRenderbuffer>(internalFormat, width, height));
		buffer->SetObjectLabel(label);
		buffer->SetAttachment(attachment);

		Bind(GL_FRAMEBUFFER);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, buffer->glHandle_);
		GL_LOG_ERRORS();
		return true;
	}

	bool GLFramebuffer::AttachRenderbuffer(GLenum internalFormat, GLsizei width, GLsizei height, GLenum attachment)
	{
		return AttachRenderbuffer(nullptr, internalFormat, width, height, attachment);
	}

	bool GLFramebuffer::DetachRenderbuffer(GLenum attachment)
	{
		for (std::uint32_t i = 0; i < attachedRenderbuffers_.size(); i++) {
			if (attachedRenderbuffers_[i]->GetAttachment() == attachment) {
				Bind(GL_FRAMEBUFFER);
				glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, 0);
				GL_LOG_ERRORS();
				attachedRenderbuffers_.eraseUnordered(i);
				return true;
			}
		}
		return false;
	}

	void GLFramebuffer::AttachTexture(GLTexture& texture, GLenum attachment)
	{
		Bind(GL_FRAMEBUFFER);
		glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, texture.target_, texture.glHandle_, 0);
		GL_LOG_ERRORS();
	}

	void GLFramebuffer::DetachTexture(GLenum attachment)
	{
		Bind(GL_FRAMEBUFFER);
		glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, 0, 0);
		GL_LOG_ERRORS();
	}

#if !(defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_ARM))
	void GLFramebuffer::Invalidate(GLsizei numAttachments, const GLenum* attachments)
	{
#if defined(RHI_GL_PROFILE_ES2)
		// glInvalidateFramebuffer() is ES 3.0 (ES2 would need EXT_discard_framebuffer, not assumed) and
		// invalidation is purely a bandwidth optimization - skipping it is always correct
		static_cast<void>(numAttachments);
		static_cast<void>(attachments);
#else
		Bind(GL_FRAMEBUFFER);
		glInvalidateFramebuffer(GL_FRAMEBUFFER, numAttachments, attachments);
		GL_LOG_ERRORS();
#endif
	}
#endif

	bool GLFramebuffer::IsStatusComplete()
	{
		Bind(GL_FRAMEBUFFER);
		const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		Unbind(GL_FRAMEBUFFER);

		return (status == GL_FRAMEBUFFER_COMPLETE);
	}

	void GLFramebuffer::SetObjectLabel(StringView label)
	{
		GLDebug::SetObjectLabel(GLDebug::LabelTypes::FrameBuffer, glHandle_, label);
	}

	bool GLFramebuffer::BindHandle(GLenum target, GLuint glHandle)
	{
		FATAL_ASSERT(target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER);

#if defined(RHI_GL_PROFILE_ES2)
		// ES2 only has the combined GL_FRAMEBUFFER target (separate READ/DRAW bind points are ES 3.0);
		// remap every request so callers like GLRenderTarget::BindDraw() and the destructor stay portable
		target = GL_FRAMEBUFFER;
#endif
		if (target == GL_FRAMEBUFFER && (readBoundBuffer_ != glHandle || drawBoundBuffer_ != glHandle)) {
			glBindFramebuffer(target, glHandle);
			GL_LOG_ERRORS();
			readBoundBuffer_ = glHandle;
			drawBoundBuffer_ = glHandle;
			return true;
		} else if (target == GL_READ_FRAMEBUFFER && readBoundBuffer_ != glHandle) {
			glBindFramebuffer(target, glHandle);
			GL_LOG_ERRORS();
			readBoundBuffer_ = glHandle;
			return true;
		} else if (target == GL_DRAW_FRAMEBUFFER && drawBoundBuffer_ != glHandle) {
			glBindFramebuffer(target, glHandle);
			GL_LOG_ERRORS();
			drawBoundBuffer_ = glHandle;
			return true;
		}
		return false;
	}

	GLuint GLFramebuffer::GetBoundHandle(GLenum target)
	{
		FATAL_ASSERT(target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER);

		if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER)
			return readBoundBuffer_;
		else
			return drawBoundBuffer_;
	}

	void GLFramebuffer::SetBoundHandle(GLenum target, GLuint glHandle)
	{
		FATAL_ASSERT(target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER);

		if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER)
			readBoundBuffer_ = glHandle;

		if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER)
			drawBoundBuffer_ = glHandle;
	}
}
