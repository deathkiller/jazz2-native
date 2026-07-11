#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RhiGL
{
	/**
		@brief Wraps an OpenGL renderbuffer object
		
		Manages the lifetime of a single OpenGL renderbuffer object and allocates its storage on
		construction with the given internal format and dimensions. Binding to `GL_RENDERBUFFER` is cached
		so that redundant `glBindRenderbuffer()` calls are skipped. Typically attached to a @ref GLFramebuffer.
	*/
	class GLRenderbuffer
	{
		friend class GLFramebuffer;

	public:
		GLRenderbuffer(GLenum internalFormat, GLsizei width, GLsizei height);
		~GLRenderbuffer();

		GLRenderbuffer(const GLRenderbuffer&) = delete;
		GLRenderbuffer& operator=(const GLRenderbuffer&) = delete;

		/** @brief Returns the OpenGL handle of the renderbuffer object */
		inline GLuint GetGLHandle() const {
			return glHandle_;
		}

		/** @brief Sets the framebuffer attachment point this renderbuffer is associated with */
		inline void SetAttachment(GLenum attachment) {
			attachment_ = attachment;
		}
		/** @brief Returns the framebuffer attachment point this renderbuffer is associated with */
		inline GLenum GetAttachment() const {
			return attachment_;
		}

		/**
		 * @brief Binds the renderbuffer to `GL_RENDERBUFFER`
		 *
		 * @return `true` if a `glBindRenderbuffer()` call was issued, `false` if it was already bound
		 */
		bool Bind() const;
		/**
		 * @brief Unbinds any renderbuffer from `GL_RENDERBUFFER`
		 *
		 * @return `true` if a `glBindRenderbuffer()` call was issued, `false` if nothing was bound
		 */
		static bool Unbind();

		/** @brief Sets an OpenGL object label for the renderbuffer, for debugging */
		void SetObjectLabel(StringView label);

	private:
		static GLuint boundBuffer_;

		GLuint glHandle_;
		GLenum attachment_;

		void Storage(GLenum internalFormat, GLsizei width, GLsizei height);
	};

}
