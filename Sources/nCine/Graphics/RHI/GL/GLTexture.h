#pragma once

#include "GLHashMap.h"
#include "../RhiTypes.h"

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	class Qt5GfxDevice;
	class ImGuiDrawing;
}

namespace nCine::RhiGL
{
	/**
		@brief Wraps an OpenGL texture object

		Manages the lifetime of a single OpenGL texture object of a given @ref TextureTarget. Binding is
		cached per texture unit, and the currently active unit is tracked, so that redundant
		`glActiveTexture()` and `glBindTexture()` calls are skipped. Provides image upload, immutable
		storage allocation, readback and parameter setting through a backend-neutral surface (aliased as
		`Rhi::Texture`): all OpenGL formats, filters, wrap and swizzle enums are translated inside the
		backend, so callers only ever pass @ref PixelFormat, @ref SamplerFilter, @ref SamplerWrapping and
		@ref SwizzleChannel values.
	*/
	class GLTexture
	{
		friend class GLFramebuffer;
		friend class nCine::Qt5GfxDevice;
		friend class nCine::ImGuiDrawing;

	public:
		/** @brief Number of texture units tracked by the bind cache */
		static constexpr std::uint32_t MaxTextureUnits = 8;

		explicit GLTexture(TextureTarget target);
		~GLTexture();

		GLTexture(const GLTexture&) = delete;
		GLTexture& operator=(const GLTexture&) = delete;

		/** @brief Returns the OpenGL handle of the texture object */
		inline GLuint GetGLHandle() const {
			return glHandle_;
		}
		/** @brief Returns a backend-neutral identifier uniquely identifying the texture (feeds material sort keys) */
		inline std::uint32_t GetUniqueId() const {
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
		void TexImage2D(std::int32_t level, PixelFormat format, bool bgr, std::int32_t width, std::int32_t height, const void* data);
		/** @brief Updates a rectangular subregion of a two-dimensional mipmap level */
		void TexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, bool bgr, const void* data);
		/** @brief Specifies a compressed two-dimensional image for the given mipmap level */
		void CompressedTexImage2D(std::int32_t level, PixelFormat format, std::int32_t width, std::int32_t height, std::int32_t imageSize, const void* data);
		/** @brief Updates a rectangular subregion of a compressed two-dimensional mipmap level */
		void CompressedTexSubImage2D(std::int32_t level, std::int32_t xoffset, std::int32_t yoffset, std::int32_t width, std::int32_t height, PixelFormat format, std::int32_t imageSize, const void* data);
		/** @brief Allocates an immutable two-dimensional storage with the given number of mipmap levels */
		void TexStorage2D(std::int32_t levels, PixelFormat format, std::int32_t width, std::int32_t height);

		/** @brief Reads back a mipmap level of the texture into client memory (desktop only) */
		void GetTexImage(std::int32_t level, PixelFormat format, bool bgr, void* pixels);

		/** @brief Sets the minification filtering mode */
		void SetMinFiltering(SamplerFilter filter);
		/** @brief Sets the magnification filtering mode */
		void SetMagFiltering(SamplerFilter filter);
		/** @brief Sets the wrapping mode for both `s` and `t` coordinates */
		void SetWrap(SamplerWrapping wrap);
		/** @brief Remaps the channels returned when the texture is sampled */
		void SetSwizzle(SwizzleChannel r, SwizzleChannel g, SwizzleChannel b, SwizzleChannel a);
		/** @brief Sets the highest mipmap level that is defined */
		void SetMaxLevel(std::int32_t maxLevel);
		/** @brief Sets the byte alignment of client pixel rows read by the upload calls */
		static void SetUnpackAlignment(std::int32_t alignment);

		/** @brief Sets a floating-point texture parameter */
		void TexParameterf(GLenum pname, GLfloat param);
		/** @brief Sets an integer texture parameter (e.g., filter or wrap mode) */
		void TexParameteri(GLenum pname, GLint param);

		/** @brief Sets an OpenGL object label for the texture, for debugging */
		void SetObjectLabel(StringView label);

		/** @brief Returns `true` if immutable texture storage (`TexStorage2D`) is available */
		static bool SupportsImmutableStorage();
		/** @brief Returns `true` if the device can read texel data back into client memory */
		static bool SupportsTextureReadback();

		/** @brief Discards any pending device error state */
		static void ClearErrors();
		/** @brief Returns `true` if the device reported an error since the last @ref ClearErrors(), and clears it */
		static bool CheckErrors();

		/** @brief Verifies that the device supports the given pixel format, aborting if it does not */
		static void CheckFormatSupport(PixelFormat format);

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
