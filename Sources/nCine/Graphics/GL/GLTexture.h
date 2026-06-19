#pragma once

#include "GLHashMap.h"

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	/**
		@brief Wraps an OpenGL texture object
		
		Manages the lifetime of a single OpenGL texture object of a given target (e.g., `GL_TEXTURE_2D`).
		Binding is cached per texture unit, and the currently active unit is tracked, so that redundant
		`glActiveTexture()` and `glBindTexture()` calls are skipped. Provides image upload, immutable
		storage allocation, readback and parameter setting.
	*/
	class GLTexture
	{
		friend class GLFramebuffer;
		friend class Qt5GfxDevice;
		friend class ImGuiDrawing;

	public:
		/** @brief Number of texture units tracked by the bind cache */
		static constexpr std::uint32_t MaxTextureUnits = 8;

		explicit GLTexture(GLenum target_);
		~GLTexture();

		GLTexture(const GLTexture&) = delete;
		GLTexture& operator=(const GLTexture&) = delete;

		/** @brief Returns the OpenGL handle of the texture object */
		inline GLuint GetGLHandle() const {
			return glHandle_;
		}
		/** @brief Returns the target of the texture (e.g., `GL_TEXTURE_2D`) */
		inline GLenum GetTarget() const {
			return target_;
		}

		/**
		 * @brief Binds the texture to the specified texture unit
		 *
		 * @return `true` if a `glBindTexture()` call was issued, `false` if it was already bound
		 */
		bool Bind(std::uint32_t textureUnit) const;
		/** @brief Binds the texture to texture unit 0 */
		inline bool Bind() const {
			return Bind(0);
		}
		/** @brief Unbinds the texture from the unit it was last bound to */
		bool Unbind() const;
		/** @brief Unbinds any texture of the given target from the specified texture unit */
		static bool Unbind(GLenum target, std::uint32_t textureUnit);
		/** @brief Unbinds any `GL_TEXTURE_2D` texture from the specified texture unit */
		static bool Unbind(std::uint32_t textureUnit);

		/** @brief Specifies a two-dimensional image for the given mipmap level */
		void TexImage2D(GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* data);
		/** @brief Updates a rectangular subregion of a two-dimensional mipmap level */
		void TexSubImage2D(GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* data);
		/** @brief Specifies a compressed two-dimensional image for the given mipmap level */
		void CompressedTexImage2D(GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLsizei imageSize, const void* data);
		/** @brief Updates a rectangular subregion of a compressed two-dimensional mipmap level */
		void CompressedTexSubImage2D(GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void* data);
		/** @brief Allocates an immutable two-dimensional storage with the given number of mipmap levels */
		void TexStorage2D(GLsizei levels, GLint internalFormat, GLsizei width, GLsizei height);

		/** @brief Reads back a mipmap level of the texture into client memory */
		void GetTexImage(GLint level, GLenum format, GLenum type, void* pixels);

		/** @brief Sets a floating-point texture parameter */
		void TexParameterf(GLenum pname, GLfloat param);
		/** @brief Sets an integer texture parameter (e.g., filter or wrap mode) */
		void TexParameteri(GLenum pname, GLint param);

		/** @brief Sets an OpenGL object label for the texture, for debugging */
		void SetObjectLabel(StringView label);

	private:
		static class GLHashMap<GLTextureMappingFunc::Size, GLTextureMappingFunc> boundTextures_[MaxTextureUnits];
		static std::uint32_t boundUnit_;

		GLuint glHandle_;
		GLenum target_;
		/**
		 * @brief Texture unit the texture was last bound to
		 *
		 * Mutable so that `const` texture objects can still be bound to a texture unit.
		 */
		mutable std::uint32_t textureUnit_;

		static bool BindHandle(GLenum target, GLuint glHandle, std::uint32_t textureUnit);
		static bool BindHandle(GLenum target, GLuint glHandle) {
			return BindHandle(target, glHandle, 0);
		}

		static GLuint GetBoundHandle(GLenum target, unsigned int textureUnit);
		static GLuint GetBoundHandle(GLenum target) {
			return GetBoundHandle(target, 0);
		}
	};
}
