#pragma once

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

namespace nCine::RHI::GL
{
	class GLRenderbuffer;
	class GLTexture;

	/**
		@brief Wraps an OpenGL framebuffer object
		
		Manages the lifetime of a single OpenGL framebuffer object (FBO) and its attachments. Renderbuffers
		are owned by the framebuffer and attached or detached as color, depth or stencil targets; textures
		can be attached as well. Binding is cached separately for the read and draw targets so that redundant
		`glBindFramebuffer()` calls are skipped. Also configures the draw buffers and checks completeness.
	*/
	class GLFramebuffer
	{
		friend class nCine::Qt5GfxDevice;

	public:
		/** @brief Maximum number of color draw buffers */
		static const std::uint32_t MaxDrawbuffers = 8;
		/** @brief Maximum number of attached renderbuffers */
		static const std::uint32_t MaxRenderbuffers = 4;

		explicit GLFramebuffer();
		~GLFramebuffer();

		GLFramebuffer(const GLFramebuffer&) = delete;
		GLFramebuffer& operator=(const GLFramebuffer&) = delete;

		/** @brief Returns the OpenGL handle of the framebuffer object */
		inline GLuint GetGLHandle() const {
			return glHandle_;
		}

		/** @brief Binds the framebuffer to both the read and draw targets */
		bool Bind() const;
		/** @brief Unbinds any framebuffer from both the read and draw targets */
		static bool Unbind();

		/** @brief Binds the framebuffer to the specified target (read, draw or both) */
		bool Bind(GLenum target) const;
		/** @brief Unbinds any framebuffer from the specified target (read, draw or both) */
		static bool Unbind(GLenum target);

		/** @brief Returns the number of active color draw buffers */
		inline std::uint32_t GetDrawbufferCount() const { return numDrawBuffers_; }
		/** @brief Sets the number of color attachments enabled as draw buffers */
		bool DrawBuffers(std::uint32_t numDrawBuffers);

		/** @brief Returns the number of attached renderbuffers */
		inline std::uint32_t GetRenderbufferCount() const { return std::uint32_t(attachedRenderbuffers_.size()); }
		/** @brief Creates a renderbuffer with a debug label and attaches it to the given attachment point */
		bool AttachRenderbuffer(const char *label, GLenum internalFormat, GLsizei width, GLsizei height, GLenum attachment);
		/** @brief Creates a renderbuffer and attaches it to the given attachment point */
		bool AttachRenderbuffer(GLenum internalFormat, GLsizei width, GLsizei height, GLenum attachment);
		/** @brief Detaches and destroys the renderbuffer at the given attachment point */
		bool DetachRenderbuffer(GLenum internalFormat);

		/** @brief Attaches a texture to the given attachment point */
		void AttachTexture(GLTexture& texture, GLenum attachment);
		/** @brief Detaches any texture from the given attachment point */
		void DetachTexture(GLenum attachment);
#if !(defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_ARM))
		/** @brief Hints that the contents of the listed attachments are no longer needed */
		void Invalidate(GLsizei numAttachments, const GLenum* attachments);
#endif

		/** @brief Returns `true` if the framebuffer is complete and ready for rendering */
		bool IsStatusComplete();

		/** @brief Sets an OpenGL object label for the framebuffer, for debugging */
		void SetObjectLabel(StringView label);

	private:
		static std::uint32_t readBoundBuffer_;
		static std::uint32_t drawBoundBuffer_;

		std::uint32_t numDrawBuffers_;
		SmallVector<std::unique_ptr<GLRenderbuffer>, MaxRenderbuffers> attachedRenderbuffers_;
		GLuint glHandle_;

		static bool BindHandle(GLenum target, GLuint glHandle);
		static GLuint GetBoundHandle(GLenum target);
		static void SetBoundHandle(GLenum target, GLuint glHandle);
	};
}
