#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"
#endif

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

		inline GLuint glHandle() const {
			return glHandle_;
		}

		inline void setAttachment(GLenum attachment) {
			attachment_ = attachment;
		}
		inline GLenum attachment() const {
			return attachment_;
		}

		bool bind() const;
		static bool unbind();

		void setObjectLabel(const char* label);

	private:
		static GLuint boundBuffer_;

		GLuint glHandle_;
		GLenum attachment_;

		void storage(GLenum internalFormat, GLsizei width, GLsizei height);
	};

}
