#pragma once

#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"

namespace nCine
{
	/// A class to handle OpenGL renderbuffer objects
	class GLRenderbuffer
	{
	public:
		inline GLuint glHandle() const {
			return glHandle_;
		}

		bool bind() const;
		static bool unbind();

	private:
		static GLuint boundBuffer_;

		GLuint glHandle_;

		GLRenderbuffer(GLenum internalFormat, GLsizei width, GLsizei height);
		~GLRenderbuffer();

		/// Deleted copy constructor
		GLRenderbuffer(const GLRenderbuffer&) = delete;
		/// Deleted assignment operator
		GLRenderbuffer& operator=(const GLRenderbuffer&) = delete;

		void storage(GLenum internalFormat, GLsizei width, GLsizei height);

		friend class GLFramebuffer;
	};

}
