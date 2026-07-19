#pragma once

#include "../RhiTypes.h"

#include <cstdint>

#include <Containers/StringView.h>

using namespace Death::Containers;

// Direct3D 11 interface referenced only as an opaque pointer here (definition pulled in by the .cpp)
struct ID3D11RenderTargetView;

namespace nCine::RhiD3D11
{
	class D3D11Texture;

	/**
		@brief Renderbuffer stub of the Direct3D 11 backend (aliased as `Rhi::Renderbuffer`)

		Carries no depth/stencil storage (the renderer is 2D); the class just records the format and
		size to satisfy the contract alias. An `ID3D11Texture2D` depth/stencil could be added here when needed.
	*/
	class D3D11Renderbuffer
	{
	public:
		D3D11Renderbuffer() = default;
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
		@brief Framebuffer stub of the Direct3D 11 backend (aliased as `Rhi::Framebuffer`)

		Provided only for the contract alias (the default-framebuffer rebinding some window backends use).
		Off-screen rendering is routed through @ref D3D11RenderTarget instead.
	*/
	class D3D11Framebuffer
	{
	public:
		D3D11Framebuffer() = default;
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
		@brief Off-screen render target of the Direct3D 11 backend (aliased as `Rhi::RenderTarget`)

		Records the color textures addressed by attachment index and an optional depth/stencil (ignored for
		2D). @ref BindDraw() records the target on the device so the following clears and draws are associated
		with its contiguously attached color attachments (0..count-1, bounded by @ref SetDrawBuffers(), the
		`glDrawBuffers` equivalent). One RTV per attachment is created lazily from the attached textures and
		`OMSetRenderTargets` is routed through here.
	*/
	class D3D11RenderTarget
	{
	public:
		static constexpr std::uint32_t MaxColorAttachments = 8;

		D3D11RenderTarget();
		~D3D11RenderTarget();

		D3D11RenderTarget(const D3D11RenderTarget&) = delete;
		D3D11RenderTarget& operator=(const D3D11RenderTarget&) = delete;

		/** @brief Returns the render target view of the given color attachment (created lazily from its texture), or `nullptr` */
		ID3D11RenderTargetView* GetRTV(std::uint32_t index = 0);
		/**
			@brief Fills @p outRtvs with the views of the contiguously attached color attachments enabled for drawing

			The count is the contiguous attached run from attachment 0, bounded by @ref SetDrawBuffers()
			(mirroring `glDrawBuffers`), and truncated at the first attachment whose view cannot be created.
			@returns The number of views written (0 if attachment 0 is missing or unusable)
		*/
		std::uint32_t GetRTVs(ID3D11RenderTargetView* outRtvs[MaxColorAttachments]);
		/** @brief Returns the number of contiguously attached color textures starting at attachment 0 */
		std::uint32_t GetAttachedCount() const;

		/** @brief Attaches a texture as the color attachment with the given index */
		void AttachColorTexture(D3D11Texture& texture, std::uint32_t index);
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
		inline D3D11Texture* GetColorTexture(std::uint32_t index) const {
			return (index < MaxColorAttachments ? colorTextures_[index] : nullptr);
		}
		/** @brief Returns the number of color attachments enabled for drawing (see @ref SetDrawBuffers()) */
		inline std::uint32_t GetNumDrawBuffers() const {
			return numDrawBuffers_;
		}

	private:
		D3D11Texture* colorTextures_[MaxColorAttachments];
		std::uint32_t numDrawBuffers_;
		// Render target view per color attachment, created lazily and invalidated when its attachment changes
		ID3D11RenderTargetView* rtvs_[MaxColorAttachments];
		D3D11Texture* rtvTextures_[MaxColorAttachments];	// the texture rtvs_[i] was built for (to detect attachment changes)

		/** @brief Releases the view of one color attachment (on attachment change and destruction) */
		void ReleaseRTV(std::uint32_t index);
	};
}
