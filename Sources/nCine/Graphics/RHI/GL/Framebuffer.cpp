#if defined(WITH_RHI_GL)
#include "Framebuffer.h"
#include "Renderbuffer.h"
#include "Texture.h"
#include "Debug.h"
#include "../../../../Main.h"

namespace nCine::RHI
{
	std::uint32_t Framebuffer::readBoundBuffer_ = 0;
	std::uint32_t Framebuffer::drawBoundBuffer_ = 0;

	Framebuffer::Framebuffer()
		: glHandle_(0), numDrawBuffers_(0)
	{
		glGenFramebuffers(1, &glHandle_);
		GL_LOG_ERRORS();
	}

	Framebuffer::~Framebuffer()
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

	bool Framebuffer::Bind() const
	{
		return Bind(GL_FRAMEBUFFER);
	}

	bool Framebuffer::Unbind()
	{
		return Unbind(GL_FRAMEBUFFER);
	}

	bool Framebuffer::Bind(GLenum target) const
	{
		return BindHandle(target, glHandle_);
	}

	bool Framebuffer::Unbind(GLenum target)
	{
		return BindHandle(target, 0);
	}

	bool Framebuffer::DrawBuffers(std::uint32_t numDrawBuffers)
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

	bool Framebuffer::AttachRenderbuffer(const char* label, GLenum internalFormat, GLsizei width, GLsizei height, GLenum attachment)
	{
		if (attachedRenderbuffers_.size() >= MaxRenderbuffers - 1) {
			return false;
		}
		for (std::uint32_t i = 0; i < attachedRenderbuffers_.size(); i++) {
			if (attachedRenderbuffers_[i]->GetAttachment() == attachment) {
				return false;
			}
		}

		std::unique_ptr<Renderbuffer>& buffer = attachedRenderbuffers_.emplace_back(std::make_unique<Renderbuffer>(internalFormat, width, height));
		buffer->SetObjectLabel(label);
		buffer->SetAttachment(attachment);

		Bind(GL_FRAMEBUFFER);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, buffer->glHandle_);
		GL_LOG_ERRORS();
		return true;
	}

	bool Framebuffer::AttachRenderbuffer(GLenum internalFormat, GLsizei width, GLsizei height, GLenum attachment)
	{
		return AttachRenderbuffer(nullptr, internalFormat, width, height, attachment);
	}

	bool Framebuffer::DetachRenderbuffer(GLenum attachment)
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

	void Framebuffer::AttachTexture(Texture& texture, GLenum attachment)
	{
		Bind(GL_FRAMEBUFFER);
		glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, texture.target_, texture.glHandle_, 0);
		GL_LOG_ERRORS();
	}

	void Framebuffer::DetachTexture(GLenum attachment)
	{
		Bind(GL_FRAMEBUFFER);
		glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, 0, 0);
		GL_LOG_ERRORS();
	}

#if !(defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_ARM))
	void Framebuffer::Invalidate(GLsizei numAttachments, const GLenum* attachments)
	{
		Bind(GL_FRAMEBUFFER);
		glInvalidateFramebuffer(GL_FRAMEBUFFER, numAttachments, attachments);
		GL_LOG_ERRORS();
	}
#endif

	bool Framebuffer::IsStatusComplete()
	{
		Bind(GL_FRAMEBUFFER);
		const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		Unbind(GL_FRAMEBUFFER);

		return (status == GL_FRAMEBUFFER_COMPLETE);
	}

	void Framebuffer::SetObjectLabel(StringView label)
	{
		Debug::SetObjectLabel(Debug::LabelTypes::FrameBuffer, glHandle_, label);
	}

	bool Framebuffer::BindHandle(GLenum target, GLuint glHandle)
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

	GLuint Framebuffer::GetBoundHandle(GLenum target)
	{
		FATAL_ASSERT(target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER);

		if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER)
			return readBoundBuffer_;
		else
			return drawBoundBuffer_;
	}

	void Framebuffer::SetBoundHandle(GLenum target, GLuint glHandle)
	{
		FATAL_ASSERT(target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER);

		if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER)
			readBoundBuffer_ = glHandle;

		if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER)
			drawBoundBuffer_ = glHandle;
	}
}

#endif // defined(WITH_RHI_GL)
