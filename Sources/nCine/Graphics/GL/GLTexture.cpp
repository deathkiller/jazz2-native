#include "GLTexture.h"
#include "GLDebug.h"
#include "../../tracy_opengl.h"

namespace nCine
{
	GLHashMap<GLTextureMappingFunc::Size, GLTextureMappingFunc> GLTexture::boundTextures_[MaxTextureUnits];
	std::uint32_t GLTexture::boundUnit_ = 0;

	GLTexture::GLTexture(GLenum target)
		: glHandle_(0), target_(target), textureUnit_(0)
	{
		glGenTextures(1, &glHandle_);
		GL_LOG_ERRORS();
	}

	GLTexture::~GLTexture()
	{
		if (boundTextures_[boundUnit_][target_] == glHandle_) {
			Unbind();
		}

		glDeleteTextures(1, &glHandle_);
		GL_LOG_ERRORS();
	}

	bool GLTexture::Bind(std::uint32_t textureUnit) const
	{
		const bool hasBound = BindHandle(target_, glHandle_, textureUnit);
		if (hasBound) {
			textureUnit_ = textureUnit;
		}
		return hasBound;
	}

	bool GLTexture::Unbind() const
	{
		return BindHandle(target_, 0, textureUnit_);
	}

	bool GLTexture::Unbind(GLenum target, std::uint32_t textureUnit)
	{
		return BindHandle(target, 0, textureUnit);
	}

	bool GLTexture::Unbind(std::uint32_t textureUnit)
	{
		return BindHandle(GL_TEXTURE_2D, 0, textureUnit);
	}

	void GLTexture::TexImage2D(GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* data)
	{
		TracyGpuZone("glTexImage2D");
		Bind();
		glTexImage2D(target_, level, internalFormat, width, height, 0, format, type, data);
		GL_LOG_ERRORS();
	}

	void GLTexture::TexSubImage2D(GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* data)
	{
		TracyGpuZone("glTexSubImage2D");
		Bind();
		glTexSubImage2D(target_, level, xoffset, yoffset, width, height, format, type, data);
		GL_LOG_ERRORS();
	}

	void GLTexture::CompressedTexImage2D(GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLsizei imageSize, const void* data)
	{
		TracyGpuZone("glCompressedTexImage2D");
		Bind();
		glCompressedTexImage2D(target_, level, internalFormat, width, height, 0, imageSize, data);
		GL_LOG_ERRORS();
	}

	void GLTexture::CompressedTexSubImage2D(GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void* data)
	{
#if !defined(WITH_OPENGL2)
		TracyGpuZone("glCompressedTexSubImage2D");
		Bind();
		glCompressedTexSubImage2D(target_, level, xoffset, yoffset, width, height, format, imageSize, data);
		GL_LOG_ERRORS();
#endif
	}

	void GLTexture::TexStorage2D(GLsizei levels, GLint internalFormat, GLsizei width, GLsizei height)
	{
#if !defined(WITH_OPENGL2)
		TracyGpuZone("glTexStorage2D");
		Bind();
		glTexStorage2D(target_, levels, internalFormat, width, height);
		GL_LOG_ERRORS();
#endif
	}

#if !defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN)
	void GLTexture::GetTexImage(GLint level, GLenum format, GLenum type, void* pixels)
	{
		TracyGpuZone("glGetTexImage");
		Bind();
		glGetTexImage(target_, level, format, type, pixels);
		GL_LOG_ERRORS();
	}
#endif

	void GLTexture::TexParameterf(GLenum pname, GLfloat param)
	{
		Bind();
		glTexParameterf(target_, pname, param);
		GL_LOG_ERRORS();
	}

	void GLTexture::TexParameteri(GLenum pname, GLint param)
	{
		Bind();
		glTexParameteri(target_, pname, param);
		GL_LOG_ERRORS();
	}

	void GLTexture::SetObjectLabel(StringView label)
	{
		GLDebug::SetObjectLabel(GLDebug::LabelTypes::Texture, glHandle_, label);
	}

	bool GLTexture::BindHandle(GLenum target, GLuint glHandle, std::uint32_t textureUnit)
	{
		FATAL_ASSERT(textureUnit < MaxTextureUnits);

		if (boundTextures_[textureUnit][target] != glHandle) {
			if (boundUnit_ != textureUnit) {
				glActiveTexture(GL_TEXTURE0 + textureUnit);
				boundUnit_ = textureUnit;
			}

			glBindTexture(target, glHandle);
			GL_LOG_ERRORS();
			boundTextures_[textureUnit][target] = glHandle;
			return true;
		}
		return false;
	}

	GLuint GLTexture::GetBoundHandle(GLenum target, unsigned int textureUnit)
	{
		FATAL_ASSERT(textureUnit < MaxTextureUnits);
		return boundTextures_[textureUnit][target];
	}
}
