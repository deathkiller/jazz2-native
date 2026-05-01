#if defined(WITH_RHI_GL)
#include "Renderbuffer.h"
#include "Debug.h"

namespace nCine::RHI
{
	GLuint Renderbuffer::boundBuffer_ = 0;

	Renderbuffer::Renderbuffer(GLenum internalFormat, GLsizei width, GLsizei height)
		: glHandle_(0), attachment_(GL_NONE)
	{
		glGenRenderbuffers(1, &glHandle_);
		Storage(internalFormat, width, height);
		GL_LOG_ERRORS();
	}

	Renderbuffer::~Renderbuffer()
	{
		if (boundBuffer_ == glHandle_) {
			Unbind();
		}
		glDeleteRenderbuffers(1, &glHandle_);
		GL_LOG_ERRORS();
	}

	bool Renderbuffer::Bind() const
	{
		if (boundBuffer_ != glHandle_) {
			glBindRenderbuffer(GL_RENDERBUFFER, glHandle_);
			GL_LOG_ERRORS();
			boundBuffer_ = glHandle_;
			return true;
		}
		return false;
	}

	bool Renderbuffer::Unbind()
	{
		if (boundBuffer_ != 0) {
			glBindRenderbuffer(GL_RENDERBUFFER, 0);
			GL_LOG_ERRORS();
			boundBuffer_ = 0;
			return true;
		}
		return false;
	}

	void Renderbuffer::SetObjectLabel(StringView label)
	{
		Debug::SetObjectLabel(Debug::LabelTypes::RenderBuffer, glHandle_, label);
	}

	void Renderbuffer::Storage(GLenum internalFormat, GLsizei width, GLsizei height)
	{
		Bind();
		glRenderbufferStorage(GL_RENDERBUFFER, internalFormat, width, height);
		Unbind();
		GL_LOG_ERRORS();
	}
}

#endif // defined(WITH_RHI_GL)
