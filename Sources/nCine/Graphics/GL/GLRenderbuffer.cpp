#include "GLRenderbuffer.h"
#include "GLDebug.h"

namespace nCine
{
	GLuint GLRenderbuffer::boundBuffer_ = 0;

	GLRenderbuffer::GLRenderbuffer(GLenum internalFormat, GLsizei width, GLsizei height)
		: glHandle_(0), attachment_(GL_NONE)
	{
		glGenRenderbuffers(1, &glHandle_);
		Storage(internalFormat, width, height);
		GL_LOG_ERRORS();
	}

	GLRenderbuffer::~GLRenderbuffer()
	{
		if (boundBuffer_ == glHandle_) {
			Unbind();
		}
		glDeleteRenderbuffers(1, &glHandle_);
		GL_LOG_ERRORS();
	}

	bool GLRenderbuffer::Bind() const
	{
		if (boundBuffer_ != glHandle_) {
			glBindRenderbuffer(GL_RENDERBUFFER, glHandle_);
			GL_LOG_ERRORS();
			boundBuffer_ = glHandle_;
			return true;
		}
		return false;
	}

	bool GLRenderbuffer::Unbind()
	{
		if (boundBuffer_ != 0) {
			glBindRenderbuffer(GL_RENDERBUFFER, 0);
			GL_LOG_ERRORS();
			boundBuffer_ = 0;
			return true;
		}
		return false;
	}

	void GLRenderbuffer::SetObjectLabel(const char* label)
	{
		GLDebug::SetObjectLabel(GLDebug::LabelTypes::RenderBuffer, glHandle_, label);
	}

	void GLRenderbuffer::Storage(GLenum internalFormat, GLsizei width, GLsizei height)
	{
		Bind();
		glRenderbufferStorage(GL_RENDERBUFFER, internalFormat, width, height);
		Unbind();
		GL_LOG_ERRORS();
	}
}
