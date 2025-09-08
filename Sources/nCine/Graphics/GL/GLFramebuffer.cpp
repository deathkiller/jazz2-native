#include "GLFramebuffer.h"
#include "GLRenderbuffer.h"
#include "GLTexture.h"
#include "GLDebug.h"
#include "../../../Main.h"

namespace nCine
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
		static const GLenum drawBuffers[MaxDrawbuffers] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3,
															GL_COLOR_ATTACHMENT4, GL_COLOR_ATTACHMENT5, GL_COLOR_ATTACHMENT6, GL_COLOR_ATTACHMENT7 };
		if (numDrawBuffers < MaxDrawbuffers && numDrawBuffers_ != numDrawBuffers) {
			glDrawBuffers(numDrawBuffers, drawBuffers);
			GL_LOG_ERRORS();
			numDrawBuffers_ = numDrawBuffers;
			return true;
		}
		return false;
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
		Bind(GL_FRAMEBUFFER);
		glInvalidateFramebuffer(GL_FRAMEBUFFER, numAttachments, attachments);
		GL_LOG_ERRORS();
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
