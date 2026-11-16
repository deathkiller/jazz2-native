#pragma once

#include "../RhiTypes.h"

#include <cstdint>

#include <Containers/StringView.h>

#include <gx2/surface.h>

using namespace Death::Containers;

namespace nCine::RHI::GX2
{
	class Gx2Texture;

	/**
		@brief Renderbuffer stub of the GX2 backend (aliased as `RHI::Renderbuffer`)

		The engine only ever asks for depth/stencil renderbuffers, and this backend gives every target the
		device's one shared depth surface (see @ref Gx2RenderTarget), so nothing is allocated here.
	*/
	class Gx2Renderbuffer
	{
	public:
		Gx2Renderbuffer() = default;
		void Create(DepthStencilFormat format, std::int32_t width, std::int32_t height) {
			static_cast<void>(format);
			_width = width;
			_height = height;
		}
		inline std::uint32_t GetGLHandle() const {
			return 0;
		}

	private:
		std::int32_t _width = 0;
		std::int32_t _height = 0;
	};

	/**
		@brief Framebuffer stub of the GX2 backend (aliased as `RHI::Framebuffer`)

		GX2 has no framebuffer object; @ref Gx2RenderTarget records the attachment directly.
	*/
	class Gx2Framebuffer
	{
	public:
		Gx2Framebuffer() = default;
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
		@brief Off-screen render target of the GX2 backend (aliased as `RHI::RenderTarget`)

		Records the colour texture the following draws render into and keeps the `GX2ColorBuffer` built over
		its surface. @ref BindDraw() records the target on the device, which binds that colour buffer with
		`GX2SetColorBuffer()` before the next clear or draw. The colour buffer and the texture share one
		tiled surface (see @ref Gx2Texture), so what the GPU renders here is what a later pass samples.

		Like the RSX, Latte is an immediate-mode renderer: switching targets is a state change, with no
		scene to begin and end and no risk of discarding what an earlier pass drew.

		<b>One colour attachment</b>, which is all the render pipeline uses.

		<b>No depth storage.</b> The engine is a 2D renderer and never enables the depth test, so neither the
		screen nor any target has a depth buffer; depth clears are ignored by the device.
	*/
	class Gx2RenderTarget
	{
	public:
		/** @brief Number of colour attachments a target can carry (see the class documentation) */
		static constexpr std::uint32_t MaxColorAttachments = 1;

		Gx2RenderTarget();
		~Gx2RenderTarget();

		Gx2RenderTarget(const Gx2RenderTarget&) = delete;
		Gx2RenderTarget& operator=(const Gx2RenderTarget&) = delete;

		/** @brief Attaches a texture as the colour attachment with the given index */
		void AttachColorTexture(Gx2Texture& texture, std::uint32_t index);
		/** @brief Detaches any texture from the colour attachment with the given index */
		void DetachColorTexture(std::uint32_t index);
		/** @brief Drops a texture that is being destroyed from the attachments, without calling back into it */
		void ForgetColorTexture(const Gx2Texture& texture);

		/** @brief Records a depth/stencil buffer (no storage is created, see the class documentation) */
		void AttachDepthStencil(DepthStencilFormat format, std::int32_t width, std::int32_t height);
		/** @brief Clears the recorded depth/stencil buffer */
		void DetachDepthStencil(DepthStencilFormat format);

		/** @brief Binds the render target as the current draw target on the device */
		void BindDraw();
		/** @brief Unbinds any render target from the device */
		static void UnbindDraw();
		/** @brief Sets the number of colour attachments enabled for drawing */
		bool SetDrawBuffers(std::uint32_t numColorAttachments);

		/** @brief Returns `true` if the target has a usable colour attachment 0 */
		bool IsStatusComplete();

		/** @brief Hints that the depth/stencil contents are no longer needed (no-op: they are never stored) */
		void InvalidateDepthStencil(DepthStencilFormat format);

		/** @brief Sets a debug label for the render target (ignored) */
		void SetObjectLabel(StringView label);

		/** @brief Returns the texture attached at the given colour attachment index, or `nullptr` */
		inline Gx2Texture* GetColorTexture(std::uint32_t index) const {
			return (index < MaxColorAttachments ? _colorTextures[index] : nullptr);
		}
		/** @brief Returns the number of colour attachments enabled for drawing (see @ref SetDrawBuffers()) */
		inline std::uint32_t GetNumDrawBuffers() const {
			return _numDrawBuffers;
		}

		/**
			@brief Returns the colour buffer the device binds to render into this target

			Built on demand over the attached texture's GPU surface and rebuilt when the attachment or its
			storage changes. @returns `nullptr` if the target has no usable attachment.
		*/
		const GX2ColorBuffer* GetColorBuffer(std::int32_t& width, std::int32_t& height);

	private:
		Gx2Texture* _colorTextures[MaxColorAttachments];
		std::uint32_t _numDrawBuffers;

		// Colour buffer over the current attachment, rebuilt when it changes
		GX2ColorBuffer _colorBuffer;
		const Gx2Texture* _surfaceTexture;	// the texture the colour buffer was built over (to detect a change)
		void* _surfaceImage;				// its GPU address at that point (a reallocation invalidates it)
		std::int32_t _surfaceWidth;
		std::int32_t _surfaceHeight;
		bool _surfaceValid;
	};
}
