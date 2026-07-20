#pragma once

#include "../RhiTypes.h"

#include <cstdint>

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI::Vulkan
{
	class VulkanTexture;

	/**
		@brief Renderbuffer stub of the Vulkan backend (aliased as `RHI::Renderbuffer`)

		Carries no depth/stencil storage (the renderer is 2D); the class just records the format and
		size to satisfy the contract alias. A depth/stencil `VkImage` could be added here when needed.
	*/
	class VulkanRenderbuffer
	{
	public:
		VulkanRenderbuffer() = default;
		void Create(DepthStencilFormat format, std::int32_t width, std::int32_t height) {
			format_ = format;
			width_ = width;
			height_ = height;
		}
		inline std::uint32_t GetGLHandle() const {
			return 0;
		}

	private:
		DepthStencilFormat format_ = DepthStencilFormat::None;
		std::int32_t width_ = 0;
		std::int32_t height_ = 0;
	};

	/**
		@brief Framebuffer stub of the Vulkan backend (aliased as `RHI::Framebuffer`)

		Provided only for the contract alias (the default-framebuffer rebinding some window backends use).
		Off-screen rendering is routed through @ref VulkanRenderTarget instead.
	*/
	class VulkanFramebuffer
	{
	public:
		VulkanFramebuffer() = default;
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
		@brief Off-screen render target of the Vulkan backend (aliased as `RHI::RenderTarget`)

		Records the color textures addressed by attachment index and an optional depth/stencil (ignored for
		2D). @ref BindDraw() records the target on the device so the following clears and draws are associated
		with its color attachments. The backend builds a `VkFramebuffer` over the image views of every
		contiguously attached color texture (against a device-provided render pass cached by the full
		attachment-format signature, so multiple render targets work); the device begins a render pass on it
		for the draws routed here.
	*/
	class VulkanRenderTarget
	{
	public:
		static constexpr std::uint32_t MaxColorAttachments = 8;

		VulkanRenderTarget();
		~VulkanRenderTarget();

		VulkanRenderTarget(const VulkanRenderTarget&) = delete;
		VulkanRenderTarget& operator=(const VulkanRenderTarget&) = delete;

		/** @brief Returns (building/caching on demand) the `VkFramebuffer` over all contiguously attached color textures, or `0` if incomplete. Opaque `std::uint64_t` to keep this header free of `vulkan.h`. */
		std::uint64_t GetFramebuffer();
		/** @brief Releases the cached framebuffer (on attachment change / destruction) */
		void ReleaseFramebuffer();

		/** @brief Attaches a texture as the color attachment with the given index */
		void AttachColorTexture(VulkanTexture& texture, std::uint32_t index);
		/** @brief Detaches any texture from the color attachment with the given index */
		void DetachColorTexture(std::uint32_t index);

		/** @brief Records a depth/stencil buffer (no storage is created) */
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
		inline VulkanTexture* GetColorTexture(std::uint32_t index) const {
			return (index < MaxColorAttachments ? colorTextures_[index] : nullptr);
		}

		/** @brief Returns the number of contiguously attached color textures starting from attachment 0 (the framebuffer / render-pass attachment count) */
		std::uint32_t GetAttachedCount() const;

		/** @brief Returns the number of color attachments enabled for drawing (see @ref SetDrawBuffers()) */
		inline std::uint32_t GetNumDrawBuffers() const {
			return numDrawBuffers_;
		}

	private:
		VulkanTexture* colorTextures_[MaxColorAttachments];
		std::uint32_t numDrawBuffers_;

		// Cached VkFramebuffer over all attached color textures (opaque; rebuilt when any attachment changes)
		std::uint64_t framebuffer_;
		std::uint64_t framebufferViews_[MaxColorAttachments];	// the image views the framebuffer was built for
		std::uint32_t framebufferViewCount_;					// their count (the framebuffer's attachment count)
	};
}
