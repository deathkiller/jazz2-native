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

		inline GLuint glHandle() const {
			return glHandle_;
		}
		inline GLenum target() const {
			return target_;
		}

		bool bind(std::uint32_t textureUnit) const;
		inline bool bind() const {
			return bind(0);
		}
		bool unbind() const;
		static bool unbind(GLenum target, std::uint32_t textureUnit);
		static bool unbind(std::uint32_t textureUnit);

		void texImage2D(GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* data);
		void texSubImage2D(GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* data);
		void compressedTexImage2D(GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLsizei imageSize, const void* data);
		void compressedTexSubImage2D(GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void* data);
		void texStorage2D(GLsizei levels, GLint internalFormat, GLsizei width, GLsizei height);

		void getTexImage(GLint level, GLenum format, GLenum type, void* pixels);

		void texParameterf(GLenum pname, GLfloat param);
		void texParameteri(GLenum pname, GLint param);

		void setObjectLabel(const char* label);

	private:
		static class GLHashMap<GLTextureMappingFunc::Size, GLTextureMappingFunc> boundTextures_[MaxTextureUnits];
		static std::uint32_t boundUnit_;

		GLuint glHandle_;
		GLenum target_;
		/// The texture unit is mutable in order for constant texture objects to be bound
		/*! A texture can be bound to a specific texture unit. */
		mutable std::uint32_t textureUnit_;

		static bool bindHandle(GLenum target, GLuint glHandle, std::uint32_t textureUnit);
		static bool bindHandle(GLenum target, GLuint glHandle) {
			return bindHandle(target, glHandle, 0);
		}
	};
}
