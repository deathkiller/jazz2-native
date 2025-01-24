#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"
#endif

#include <memory>

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	class GLRenderbuffer;
	class GLTexture;

	/// Handles OpenGL framebuffer objects
	class GLFramebuffer
	{
	public:
		static const unsigned int MaxDrawbuffers = 8;
		static const unsigned int MaxRenderbuffers = 4;

		explicit GLFramebuffer();
		~GLFramebuffer();

		inline GLuint glHandle() const {
			return glHandle_;
		}

		bool bind() const;
		static bool unbind();

		bool bind(GLenum target) const;
		static bool unbind(GLenum target);

		inline unsigned int numDrawbuffers() const { return numDrawBuffers_; }
		bool drawBuffers(unsigned int numDrawBuffers);

		inline unsigned int numRenderbuffers() const { return (unsigned int)attachedRenderbuffers_.size(); }
		bool attachRenderbuffer(const char *label, GLenum internalFormat, GLsizei width, GLsizei height, GLenum attachment);
		bool attachRenderbuffer(GLenum internalFormat, GLsizei width, GLsizei height, GLenum attachment);
		bool detachRenderbuffer(GLenum internalFormat);

		void attachTexture(GLTexture& texture, GLenum attachment);
		void detachTexture(GLenum attachment);
#if !(defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_ARM))
		void invalidate(GLsizei numAttachments, const GLenum* attachments);
#endif

		bool isStatusComplete();

		void setObjectLabel(const char* label);

	private:
		static unsigned int readBoundBuffer_;
		static unsigned int drawBoundBuffer_;
		unsigned int numDrawBuffers_;

		SmallVector<std::unique_ptr<GLRenderbuffer>, MaxRenderbuffers> attachedRenderbuffers_;

		GLuint glHandle_;

		/// Deleted copy constructor
		GLFramebuffer(const GLFramebuffer&) = delete;
		/// Deleted assignment operator
		GLFramebuffer& operator=(const GLFramebuffer&) = delete;

		static bool bindHandle(GLenum target, GLuint glHandle);

		friend class Qt5GfxDevice;
	};

}
