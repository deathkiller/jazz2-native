#include "GLRenderbuffer.h"
#include "GLDebug.h"

namespace nCine::RHI::GL
{
	GLuint GLRenderbuffer::_boundBuffer = 0;

	GLRenderbuffer::GLRenderbuffer(GLenum internalFormat, GLsizei width, GLsizei height)
		: _glHandle(0), _attachment(GL_NONE)
	{
		glGenRenderbuffers(1, &_glHandle);
		Storage(internalFormat, width, height);
		GL_LOG_ERRORS();
	}

	GLRenderbuffer::~GLRenderbuffer()
	{
		if (_boundBuffer == _glHandle) {
			Unbind();
		}
		glDeleteRenderbuffers(1, &_glHandle);
		GL_LOG_ERRORS();
	}

	bool GLRenderbuffer::Bind() const
	{
		if (_boundBuffer != _glHandle) {
			glBindRenderbuffer(GL_RENDERBUFFER, _glHandle);
			GL_LOG_ERRORS();
			_boundBuffer = _glHandle;
			return true;
		}
		return false;
	}

	bool GLRenderbuffer::Unbind()
	{
		if (_boundBuffer != 0) {
			glBindRenderbuffer(GL_RENDERBUFFER, 0);
			GL_LOG_ERRORS();
			_boundBuffer = 0;
			return true;
		}
		return false;
	}

	void GLRenderbuffer::SetObjectLabel(StringView label)
	{
		GLDebug::SetObjectLabel(GLDebug::LabelTypes::RenderBuffer, _glHandle, label);
	}

	void GLRenderbuffer::Storage(GLenum internalFormat, GLsizei width, GLsizei height)
	{
		Bind();
		glRenderbufferStorage(GL_RENDERBUFFER, internalFormat, width, height);
		Unbind();
		GL_LOG_ERRORS();
	}
}
