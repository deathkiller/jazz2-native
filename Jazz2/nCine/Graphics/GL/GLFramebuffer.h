#pragma once

#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"

#include <SmallVector.h>

using namespace Death;

namespace nCine
{
	class GLRenderbuffer;
	class GLTexture;

	/// A class to handle OpenGL framebuffer objects
	class GLFramebuffer
	{
	public:
		explicit GLFramebuffer();
		~GLFramebuffer();

		inline GLuint glHandle() const {
			return glHandle_;
		}

		bool bind() const;
		static bool unbind();

		bool bind(GLenum target) const;
		static bool unbind(GLenum target);

		void attachRenderbuffer(GLenum internalFormat, GLsizei width, GLsizei height, GLenum attachment);
		void attachTexture(GLTexture& texture, GLenum attachment);
		void invalidate(GLsizei numAttachments, const GLenum* attachments);

		bool isStatusComplete();

	private:
		static unsigned int readBoundBuffer_;
		static unsigned int drawBoundBuffer_;

		SmallVector<GLRenderbuffer*, 0> attachedRenderbuffers_;

		GLuint glHandle_;

		/// Deleted copy constructor
		GLFramebuffer(const GLFramebuffer&) = delete;
		/// Deleted assignment operator
		GLFramebuffer& operator=(const GLFramebuffer&) = delete;

		static bool bindHandle(GLenum target, GLuint glHandle);

		friend class Qt5GfxDevice;
	};

}
