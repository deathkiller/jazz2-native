#pragma once

#include "../RhiTypes.h"

#include <cstdint>

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI::RDP
{
	class RdpTexture;

	/**
		@brief Renderbuffer stub of the RDP backend (aliased as `RHI::Renderbuffer`)

		The RDP backend renders 2D only, so depth/stencil renderbuffers carry no storage; the class
		just records the format and size to satisfy the contract alias.
	*/
	class RdpRenderbuffer
	{
	public:
		RdpRenderbuffer() = default;
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
		@brief Framebuffer stub of the RDP backend (aliased as `RHI::Framebuffer`)

		Provided only for the contract alias (the default-framebuffer rebinding some window backends use).
		The RDP backend routes off-screen rendering through @ref RdpRenderTarget instead.
	*/
	class RdpFramebuffer
	{
	public:
		RdpFramebuffer() = default;
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
		@brief Off-screen render target of the RDP backend (aliased as `RHI::RenderTarget`)

		Holds the color textures the rasterizer writes into (addressed by attachment index) and an
		optional depth/stencil (ignored for 2D). @ref BindDraw() records the target on the device so the
		following clears and draws land in its color attachment 0.
	*/
	class RdpRenderTarget
	{
	public:
		static constexpr std::uint32_t MaxColorAttachments = 8;

		RdpRenderTarget();
		~RdpRenderTarget();

		RdpRenderTarget(const RdpRenderTarget&) = delete;
		RdpRenderTarget& operator=(const RdpRenderTarget&) = delete;

		/** @brief Attaches a texture as the color attachment with the given index */
		void AttachColorTexture(RdpTexture& texture, std::uint32_t index);
		/** @brief Detaches any texture from the color attachment with the given index */
		void DetachColorTexture(std::uint32_t index);

		/** @brief Records a depth/stencil buffer (no storage — the RDP backend is 2D) */
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
		inline RdpTexture* GetColorTexture(std::uint32_t index) const {
			return (index < MaxColorAttachments ? _colorTextures[index] : nullptr);
		}

	private:
		RdpTexture* _colorTextures[MaxColorAttachments];
		std::uint32_t _numDrawBuffers;
	};
}
