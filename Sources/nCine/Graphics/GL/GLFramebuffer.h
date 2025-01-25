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
		friend class Qt5GfxDevice;

	public:
		static const std::uint32_t MaxDrawbuffers = 8;
		static const std::uint32_t MaxRenderbuffers = 4;

		explicit GLFramebuffer();
		~GLFramebuffer();

		GLFramebuffer(const GLFramebuffer&) = delete;
		GLFramebuffer& operator=(const GLFramebuffer&) = delete;

		inline GLuint glHandle() const {
			return glHandle_;
		}

		bool bind() const;
		static bool unbind();

		bool bind(GLenum target) const;
		static bool unbind(GLenum target);

		inline std::uint32_t numDrawbuffers() const { return numDrawBuffers_; }
		bool drawBuffers(unsigned int numDrawBuffers);

		inline std::uint32_t numRenderbuffers() const { return std::uint32_t(attachedRenderbuffers_.size()); }
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
		static std::uint32_t readBoundBuffer_;
		static std::uint32_t drawBoundBuffer_;

		std::uint32_t numDrawBuffers_;
		SmallVector<std::unique_ptr<GLRenderbuffer>, MaxRenderbuffers> attachedRenderbuffers_;
		GLuint glHandle_;

		static bool bindHandle(GLenum target, GLuint glHandle);
	};
}
