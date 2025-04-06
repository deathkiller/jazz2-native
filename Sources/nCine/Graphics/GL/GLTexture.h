#pragma once

#include "GLHashMap.h"

namespace nCine
{
	/// Handles OpenGL 2D textures
	class GLTexture
	{
		friend class GLFramebuffer;
		friend class Qt5GfxDevice;
		friend class ImGuiDrawing;

	public:
		static constexpr std::uint32_t MaxTextureUnits = 8;

		explicit GLTexture(GLenum target_);
		~GLTexture();

		GLTexture(const GLTexture&) = delete;
		GLTexture& operator=(const GLTexture&) = delete;

		inline GLuint GetGLHandle() const {
			return glHandle_;
		}
		inline GLenum GetTarget() const {
			return target_;
		}

		bool Bind(std::uint32_t textureUnit) const;
		inline bool Bind() const {
			return Bind(0);
		}
		bool Unbind() const;
		static bool Unbind(GLenum target, std::uint32_t textureUnit);
		static bool Unbind(std::uint32_t textureUnit);

		void TexImage2D(GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* data);
		void TexSubImage2D(GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* data);
		void CompressedTexImage2D(GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLsizei imageSize, const void* data);
		void CompressedTexSubImage2D(GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void* data);
		void TexStorage2D(GLsizei levels, GLint internalFormat, GLsizei width, GLsizei height);

		void GetTexImage(GLint level, GLenum format, GLenum type, void* pixels);

		void TexParameterf(GLenum pname, GLfloat param);
		void TexParameteri(GLenum pname, GLint param);

		void SetObjectLabel(const char* label);

	private:
		static class GLHashMap<GLTextureMappingFunc::Size, GLTextureMappingFunc> boundTextures_[MaxTextureUnits];
		static std::uint32_t boundUnit_;

		GLuint glHandle_;
		GLenum target_;
		/// The texture unit is mutable in order for constant texture objects to be bound
		/*! A texture can be bound to a specific texture unit. */
		mutable std::uint32_t textureUnit_;

		static bool BindHandle(GLenum target, GLuint glHandle, std::uint32_t textureUnit);
		static bool BindHandle(GLenum target, GLuint glHandle) {
			return BindHandle(target, glHandle, 0);
		}
	};
}
