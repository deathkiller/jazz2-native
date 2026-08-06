#pragma once

#include "../RhiTypes.h"

#include <cstdint>

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI::GS
{
	class GsTexture;

	/**
		@brief Renderbuffer stub of the GS backend (aliased as `RHI::Renderbuffer`)

		The GS backend renders 2D only and runs without a Z buffer at all (the depth test is set to ALWAYS
		with writes masked, so the pointer is never touched and its pages stay free), so depth/stencil
		renderbuffers carry no storage; the class just records the format and size to satisfy the alias.
	*/
	class GsRenderbuffer
	{
	public:
		GsRenderbuffer() = default;
		void Create(DepthStencilFormat format, std::int32_t width, std::int32_t height) {
			_format = format;
			_width = width;
			_height = height;
		}
		inline std::uint32_t GetGLHandle() const {
			return 0;
		}

	private:
		DepthStencilFormat _format = DepthStencilFormat::None;
		std::int32_t _width = 0;
		std::int32_t _height = 0;
	};

	/**
		@brief Framebuffer stub of the GS backend (aliased as `RHI::Framebuffer`)

		Provided only for the contract alias (the default-framebuffer rebinding some window backends use).
		The GS backend routes off-screen rendering through @ref GsRenderTarget instead.
	*/
	class GsFramebuffer
	{
	public:
		GsFramebuffer() = default;
		inline std::uint32_t GetGLHandle() const {
			return 0;
		}
		bool Bind() const {
			return true;
		}
		static bool Unbind() {
			return true;
		}
	};

	/**
		@brief Off-screen render target of the GS backend (aliased as `RHI::RenderTarget`)

		Holds the color textures the rasterizer writes into (addressed by attachment index) and an optional
		depth/stencil (ignored for 2D). @ref BindDraw() records the target on the device, which points
		`FRAME.FBP` at the attachment's pages for the following clears and draws - on the GS switching render
		target really is nothing more than that.

		The pages themselves are allocated by the attached texture (see `GsTexture::SetRenderTarget()`) out of
		the layout's render-target reserve rather than the streaming cache, because a render target has no
		host copy to be rebuilt from and so must never be evicted.
	*/
	class GsRenderTarget
	{
	public:
		static constexpr std::uint32_t MaxColorAttachments = 8;

		GsRenderTarget();
		~GsRenderTarget();

		GsRenderTarget(const GsRenderTarget&) = delete;
		GsRenderTarget& operator=(const GsRenderTarget&) = delete;

		/** @brief Attaches a texture as the color attachment with the given index */
		void AttachColorTexture(GsTexture& texture, std::uint32_t index);
		/** @brief Detaches any texture from the color attachment with the given index */
		void DetachColorTexture(std::uint32_t index);

		/** @brief Records a depth/stencil buffer (no storage - the GS backend is 2D) */
		void AttachDepthStencil(DepthStencilFormat format, std::int32_t width, std::int32_t height);
		/** @brief Clears the recorded depth/stencil buffer */
		void DetachDepthStencil(DepthStencilFormat format);

		/** @brief Binds the render target as the current draw target on the device */
		void BindDraw();
		/** @brief Unbinds any render target from the device */
		static void UnbindDraw();
		/** @brief Sets the number of color attachments enabled for drawing */
		bool SetDrawBuffers(std::uint32_t numColorAttachments);

		/** @brief Returns `true` if the target has a usable color attachment 0 */
		bool IsStatusComplete();

		/** @brief Hints that the depth/stencil contents are no longer needed (no-op) */
		void InvalidateDepthStencil(DepthStencilFormat format);

		/** @brief Sets a debug label for the render target (ignored) */
		void SetObjectLabel(StringView label);

		/** @brief Returns the texture attached at the given color attachment index, or `nullptr` */
		inline GsTexture* GetColorTexture(std::uint32_t index) const {
			return (index < MaxColorAttachments ? _colorTextures[index] : nullptr);
		}

	private:
		GsTexture* _colorTextures[MaxColorAttachments];
		std::uint32_t _numDrawBuffers;
	};
}
