#if defined(WITH_RHI_GL)
#include "Texture.h"
#include "Debug.h"
#include "../../../tracy_opengl.h"

namespace nCine::RHI
{
	HashMap<TextureMappingFunc::Size, TextureMappingFunc> Texture::boundTextures_[MaxTextureUnits];
	std::uint32_t Texture::boundUnit_ = 0;

	Texture::Texture(GLenum target)
		: glHandle_(0), target_(target), textureUnit_(0)
	{
		glGenTextures(1, &glHandle_);
		GL_LOG_ERRORS();
	}

	Texture::~Texture()
	{
		if (boundTextures_[boundUnit_][target_] == glHandle_) {
			Unbind();
		}

		glDeleteTextures(1, &glHandle_);
		GL_LOG_ERRORS();
	}

	bool Texture::Bind(std::uint32_t textureUnit) const
	{
		const bool hasBound = BindHandle(target_, glHandle_, textureUnit);
		if (hasBound) {
			textureUnit_ = textureUnit;
		}
		return hasBound;
	}

	bool Texture::Unbind() const
	{
		return BindHandle(target_, 0, textureUnit_);
	}

	bool Texture::Unbind(GLenum target, std::uint32_t textureUnit)
	{
		return BindHandle(target, 0, textureUnit);
	}

	bool Texture::Unbind(std::uint32_t textureUnit)
	{
		return BindHandle(GL_TEXTURE_2D, 0, textureUnit);
	}

	void Texture::TexImage2D(GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* data)
	{
		TracyGpuZone("glTexImage2D");
		Bind();
		glTexImage2D(target_, level, internalFormat, width, height, 0, format, type, data);
		GL_LOG_ERRORS();
	}

	void Texture::TexSubImage2D(GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* data)
	{
		TracyGpuZone("glTexSubImage2D");
		Bind();
		glTexSubImage2D(target_, level, xoffset, yoffset, width, height, format, type, data);
		GL_LOG_ERRORS();
	}

	void Texture::CompressedTexImage2D(GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLsizei imageSize, const void* data)
	{
		TracyGpuZone("glCompressedTexImage2D");
		Bind();
		glCompressedTexImage2D(target_, level, internalFormat, width, height, 0, imageSize, data);
		GL_LOG_ERRORS();
	}

	void Texture::CompressedTexSubImage2D(GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void* data)
	{
		TracyGpuZone("glCompressedTexSubImage2D");
		Bind();
		glCompressedTexSubImage2D(target_, level, xoffset, yoffset, width, height, format, imageSize, data);
		GL_LOG_ERRORS();
	}

	void Texture::TexStorage2D(GLsizei levels, GLint internalFormat, GLsizei width, GLsizei height)
	{
		TracyGpuZone("glTexStorage2D");
		Bind();
		glTexStorage2D(target_, levels, internalFormat, width, height);
		GL_LOG_ERRORS();
	}

#if !defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN)
	void Texture::GetTexImage(GLint level, GLenum format, GLenum type, void* pixels)
	{
		TracyGpuZone("glGetTexImage");
		Bind();
		glGetTexImage(target_, level, format, type, pixels);
		GL_LOG_ERRORS();
	}
#endif

	void Texture::TexParameterf(GLenum pname, GLfloat param)
	{
		Bind();
		glTexParameterf(target_, pname, param);
		GL_LOG_ERRORS();
	}

	void Texture::TexParameteri(GLenum pname, GLint param)
	{
		Bind();
		glTexParameteri(target_, pname, param);
		GL_LOG_ERRORS();
	}

	void Texture::SetObjectLabel(StringView label)
	{
		Debug::SetObjectLabel(Debug::LabelTypes::Texture, glHandle_, label);
	}

	bool Texture::BindHandle(GLenum target, GLuint glHandle, std::uint32_t textureUnit)
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

	GLuint Texture::GetBoundHandle(GLenum target, unsigned int textureUnit)
	{
		FATAL_ASSERT(textureUnit < MaxTextureUnits);
		return boundTextures_[textureUnit][target];
	}
}

#endif // defined(WITH_RHI_GL)
