#pragma once

#include "GLFramebuffer.h"
#include "../RhiTypes.h"

namespace nCine::RhiGL
{
	/**
		@brief Off-screen render target of the OpenGL backend

		Folds a framebuffer object and its owned depth/stencil renderbuffer behind a backend-neutral
		surface (aliased as `Rhi::RenderTarget`): color attachments are textures addressed by index,
		the depth/stencil attachment is described by @ref DepthStencilFormat, and the backend translates
		both to its own attachment points and storage formats.
	*/
	class GLRenderTarget
	{
	public:
		GLRenderTarget() = default;

		GLRenderTarget(const GLRenderTarget&) = delete;
		GLRenderTarget& operator=(const GLRenderTarget&) = delete;

		/** @brief Attaches a texture as the color attachment with the given index */
		void AttachColorTexture(GLTexture& texture, std::uint32_t index);
		/** @brief Detaches any texture from the color attachment with the given index */
		void DetachColorTexture(std::uint32_t index);

		/** @brief Creates and attaches a depth/stencil buffer of the given format and size */
		void AttachDepthStencil(DepthStencilFormat format, std::int32_t width, std::int32_t height);
		/** @brief Detaches and destroys the depth/stencil buffer of the given format */
		void DetachDepthStencil(DepthStencilFormat format);

		/** @brief Binds the render target for drawing */
		void BindDraw();
		/** @brief Unbinds any render target from drawing, restoring the default one */
		static void UnbindDraw();
		/** @brief Sets the number of color attachments enabled for drawing */
		bool SetDrawBuffers(std::uint32_t numColorAttachments);

		/** @brief Returns `true` if the render target is complete and ready for rendering */
		bool IsStatusComplete();

		/** @brief Hints that the contents of the depth/stencil attachment are no longer needed */
		void InvalidateDepthStencil(DepthStencilFormat format);

		/** @brief Sets a backend object label for the render target, for debugging */
		void SetObjectLabel(StringView label);

	private:
		GLFramebuffer fbo_;
	};
}
