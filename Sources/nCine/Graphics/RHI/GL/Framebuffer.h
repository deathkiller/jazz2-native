#pragma once

#if defined(WITH_RHI_GL)

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

#include <memory>

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	class Qt5GfxDevice;
}

namespace nCine::RHI
{
	class Renderbuffer;
	class Texture;

	/// Handles OpenGL framebuffer objects
	class Framebuffer
	{
		friend class nCine::Qt5GfxDevice;

	public:
		static const std::uint32_t MaxDrawbuffers = 8;
		static const std::uint32_t MaxRenderbuffers = 4;

		explicit Framebuffer();
		~Framebuffer();

		Framebuffer(const Framebuffer&) = delete;
		Framebuffer& operator=(const Framebuffer&) = delete;

		inline GLuint GetGLHandle() const {
			return glHandle_;
		}

		bool Bind() const;
		static bool Unbind();

		bool Bind(GLenum target) const;
		static bool Unbind(GLenum target);

		inline std::uint32_t GetDrawbufferCount() const { return numDrawBuffers_; }
		bool DrawBuffers(std::uint32_t numDrawBuffers);

		inline std::uint32_t GetRenderbufferCount() const { return std::uint32_t(attachedRenderbuffers_.size()); }
		bool AttachRenderbuffer(const char *label, GLenum internalFormat, GLsizei width, GLsizei height, GLenum attachment);
		bool AttachRenderbuffer(GLenum internalFormat, GLsizei width, GLsizei height, GLenum attachment);
		bool DetachRenderbuffer(GLenum internalFormat);

		void AttachTexture(Texture& texture, GLenum attachment);
		void DetachTexture(GLenum attachment);
#if !(defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_ARM))
		void Invalidate(GLsizei numAttachments, const GLenum* attachments);
#endif

		bool IsStatusComplete();

		void SetObjectLabel(StringView label);

	private:
		static std::uint32_t readBoundBuffer_;
		static std::uint32_t drawBoundBuffer_;

		std::uint32_t numDrawBuffers_;
		SmallVector<std::unique_ptr<Renderbuffer>, MaxRenderbuffers> attachedRenderbuffers_;
		GLuint glHandle_;

		static bool BindHandle(GLenum target, GLuint glHandle);
		static GLuint GetBoundHandle(GLenum target);
		static void SetBoundHandle(GLenum target, GLuint glHandle);
	};
}

#endif // defined(WITH_RHI_GL)
