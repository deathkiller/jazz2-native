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

		Slice 2a carries no depth/stencil storage (the renderer is 2D); the class just records the format and
		size to satisfy the contract alias. Slice 2b creates an `ID3D11Texture2D` depth/stencil when needed.
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
		with its color attachment 0. Slice 2a keeps this at the recording level (the textures have no real
		`ID3D11RenderTargetView` yet); slice 2b creates the RTVs and routes `OMSetRenderTargets` through here.
	*/
	class D3D11RenderTarget
	{
	public:
		static constexpr std::uint32_t MaxColorAttachments = 8;

		D3D11RenderTarget();
		~D3D11RenderTarget();

		D3D11RenderTarget(const D3D11RenderTarget&) = delete;
		D3D11RenderTarget& operator=(const D3D11RenderTarget&) = delete;

		/** @brief Returns the render target view of color attachment 0 (created lazily from its texture), or `nullptr` */
		ID3D11RenderTargetView* GetRTV();

		/** @brief Attaches a texture as the color attachment with the given index */
		void AttachColorTexture(D3D11Texture& texture, std::uint32_t index);
		/** @brief Detaches any texture from the color attachment with the given index */
		void DetachColorTexture(std::uint32_t index);

		/** @brief Records a depth/stencil buffer (no storage in slice 2a) */
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

	private:
		D3D11Texture* colorTextures_[MaxColorAttachments];
		std::uint32_t numDrawBuffers_;
		// Render target view of color attachment 0, created lazily and invalidated when the attachment changes
		ID3D11RenderTargetView* rtv_;
		D3D11Texture* rtvTexture_;		// the texture rtv_ was built for (to detect attachment changes)
	};
}
