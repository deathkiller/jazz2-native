#include "GLFramebuffer.h"

#include "GLRenderbuffer.h"
#include "GLTexture.h"

namespace nCine {

	///////////////////////////////////////////////////////////
	// STATIC DEFINITIONS
	///////////////////////////////////////////////////////////

	unsigned int GLFramebuffer::readBoundBuffer_ = 0;
	unsigned int GLFramebuffer::drawBoundBuffer_ = 0;

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	GLFramebuffer::GLFramebuffer()
		: attachedRenderbuffers_(4), glHandle_(0)
	{
		glGenFramebuffers(1, &glHandle_);
	}

	GLFramebuffer::~GLFramebuffer()
	{
		if (readBoundBuffer_ == glHandle_)
			unbind(GL_READ_FRAMEBUFFER);
		if (drawBoundBuffer_ == glHandle_)
			unbind(GL_DRAW_FRAMEBUFFER);

		for (GLRenderbuffer* attachedRenderbuffer : attachedRenderbuffers_)
			delete attachedRenderbuffer;

		glDeleteFramebuffers(1, &glHandle_);
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	bool GLFramebuffer::bind() const
	{
		return bind(GL_FRAMEBUFFER);
	}

	bool GLFramebuffer::unbind()
	{
		return unbind(GL_FRAMEBUFFER);
	}

	bool GLFramebuffer::bind(GLenum target) const
	{
		return bindHandle(target, glHandle_);
	}

	bool GLFramebuffer::unbind(GLenum target)
	{
		return bindHandle(target, 0);
	}

	void GLFramebuffer::attachRenderbuffer(GLenum internalFormat, GLsizei width, GLsizei height, GLenum attachment)
	{
		GLRenderbuffer* renderBuffer = new GLRenderbuffer(internalFormat, width, height);
		attachedRenderbuffers_.push_back(renderBuffer);

		bind(GL_FRAMEBUFFER);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, renderBuffer->glHandle_);
	}

	void GLFramebuffer::attachTexture(GLTexture& texture, GLenum attachment)
	{
		bind(GL_FRAMEBUFFER);
		glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, texture.target_, texture.glHandle_, 0);
	}

	void GLFramebuffer::invalidate(GLsizei numAttachments, const GLenum* attachments)
	{
		bind(GL_FRAMEBUFFER);
		glInvalidateFramebuffer(GL_FRAMEBUFFER, numAttachments, attachments);
	}

	bool GLFramebuffer::isStatusComplete()
	{
		bind(GL_FRAMEBUFFER);
		const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		unbind(GL_FRAMEBUFFER);

		return (status == GL_FRAMEBUFFER_COMPLETE);
	}

	///////////////////////////////////////////////////////////
	// PRIVATE FUNCTIONS
	///////////////////////////////////////////////////////////

	bool GLFramebuffer::bindHandle(GLenum target, GLuint glHandle)
	{
		//FATAL_ASSERT(target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER);

		if (target == GL_FRAMEBUFFER &&
			(readBoundBuffer_ != glHandle || drawBoundBuffer_ != glHandle)) {
			glBindFramebuffer(target, glHandle);
			readBoundBuffer_ = glHandle;
			drawBoundBuffer_ = glHandle;
			return true;
		} else if (target == GL_READ_FRAMEBUFFER && readBoundBuffer_ != glHandle) {
			glBindFramebuffer(target, glHandle);
			readBoundBuffer_ = glHandle;
			return true;
		} else if (target == GL_DRAW_FRAMEBUFFER && drawBoundBuffer_ != glHandle) {
			glBindFramebuffer(target, glHandle);
			drawBoundBuffer_ = glHandle;
			return true;
		}
		return false;
	}

}
