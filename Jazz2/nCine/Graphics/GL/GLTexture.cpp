#include "GLTexture.h"
#include "GLDebug.h"

namespace nCine
{
	///////////////////////////////////////////////////////////
	// STATIC DEFINITIONS
	///////////////////////////////////////////////////////////

	GLHashMap<GLTextureMappingFunc::Size, GLTextureMappingFunc> GLTexture::boundTextures_[MaxTextureUnits];
	unsigned int GLTexture::boundUnit_ = 0;

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS AND DESTRUCTOR
	///////////////////////////////////////////////////////////

	GLTexture::GLTexture(GLenum target)
		: glHandle_(0), target_(target), textureUnit_(0)
	{
		glGenTextures(1, &glHandle_);
	}

	GLTexture::~GLTexture()
	{
		if (boundTextures_[boundUnit_][target_] == glHandle_)
			unbind();

		glDeleteTextures(1, &glHandle_);
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	bool GLTexture::bind(unsigned int textureUnit) const
	{
		const bool hasBound = bindHandle(target_, glHandle_, textureUnit);
		if (hasBound)
			textureUnit_ = textureUnit;
		return hasBound;
	}

	bool GLTexture::unbind() const
	{
		return bindHandle(target_, 0, textureUnit_);
	}

	void GLTexture::texImage2D(GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* data)
	{
		bind();
		glTexImage2D(target_, level, internalFormat, width, height, 0, format, type, data);
	}

	void GLTexture::texSubImage2D(GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* data)
	{
		bind();
		glTexSubImage2D(target_, level, xoffset, yoffset, width, height, format, type, data);
	}

	void GLTexture::compressedTexImage2D(GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLsizei imageSize, const void* data)
	{
		bind();
		glCompressedTexImage2D(target_, level, internalFormat, width, height, 0, imageSize, data);
	}

	void GLTexture::compressedTexSubImage2D(GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void* data)
	{
		bind();
		glCompressedTexSubImage2D(target_, level, xoffset, yoffset, width, height, format, imageSize, data);
	}

	void GLTexture::texStorage2D(GLsizei levels, GLint internalFormat, GLsizei width, GLsizei height)
	{
		bind();
		glTexStorage2D(target_, levels, internalFormat, width, height);
	}

#if !defined(WITH_OPENGLES) && !defined(__EMSCRIPTEN__)
	void GLTexture::getTexImage(GLint level, GLenum format, GLenum type, void* pixels)
	{
		bind();
		glGetTexImage(target_, level, format, type, pixels);
	}
#endif

	void GLTexture::texParameterf(GLenum pname, GLfloat param)
	{
		bind();
		glTexParameterf(target_, pname, param);
	}

	void GLTexture::texParameteri(GLenum pname, GLint param)
	{
		bind();
		glTexParameteri(target_, pname, param);
	}

	void GLTexture::setObjectLabel(const char* label)
	{
		GLDebug::objectLabel(GLDebug::LabelTypes::TEXTURE, glHandle_, label);
	}

	///////////////////////////////////////////////////////////
	// PRIVATE FUNCTIONS
	///////////////////////////////////////////////////////////

	bool GLTexture::bindHandle(GLenum target, GLuint glHandle, unsigned int textureUnit)
	{
		//FATAL_ASSERT(textureUnit < MaxTextureUnits);

		if (boundTextures_[textureUnit][target] != glHandle) {
			if (boundUnit_ != textureUnit) {
				glActiveTexture(GL_TEXTURE0 + textureUnit);
				boundUnit_ = textureUnit;
			}

			glBindTexture(target, glHandle);
			boundTextures_[textureUnit][target] = glHandle;
			return true;
		}
		return false;
	}

}
