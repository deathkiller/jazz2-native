#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"
#endif

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	/// Handles OpenGL renderbuffer objects
	class GLRenderbuffer
	{
		friend class GLFramebuffer;

	public:
		GLRenderbuffer(GLenum internalFormat, GLsizei width, GLsizei height);
		~GLRenderbuffer();

		GLRenderbuffer(const GLRenderbuffer&) = delete;
		GLRenderbuffer& operator=(const GLRenderbuffer&) = delete;

		inline GLuint GetGLHandle() const {
			return glHandle_;
		}

		inline void SetAttachment(GLenum attachment) {
			attachment_ = attachment;
		}
		inline GLenum GetAttachment() const {
			return attachment_;
		}

		bool Bind() const;
		static bool Unbind();

		void SetObjectLabel(StringView label);

	private:
		static GLuint boundBuffer_;

		GLuint glHandle_;
		GLenum attachment_;

		void Storage(GLenum internalFormat, GLsizei width, GLsizei height);
	};

}
