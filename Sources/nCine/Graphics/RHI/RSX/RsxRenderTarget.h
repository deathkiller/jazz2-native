#pragma once

#include "../RhiTypes.h"

#include <cstdint>

#include <Containers/StringView.h>

#include <rsx/gcm_sys.h>

using namespace Death::Containers;

namespace nCine::RHI::RSX
{
	class RsxTexture;

	/**
		@brief Renderbuffer stub of the RSX backend (aliased as `RHI::Renderbuffer`)

		The engine only ever asks for depth/stencil renderbuffers, and this backend gives every target the
		device's one shared depth surface (see @ref RsxRenderTarget), so nothing is allocated here.
	*/
	class RsxRenderbuffer
	{
	public:
		RsxRenderbuffer() = default;
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
		@brief Framebuffer stub of the RSX backend (aliased as `RHI::Framebuffer`)

		The RSX has no framebuffer object; @ref RsxRenderTarget records the attachment directly.
	*/
	class RsxFramebuffer
	{
	public:
		RsxFramebuffer() = default;
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
		@brief Off-screen render target of the RSX backend (aliased as `RHI::RenderTarget`)

		Records the colour texture the following draws render into and keeps the `gcmSurface` that describes
		it to the hardware. @ref BindDraw() records the target on the device, which programs that surface
		with `rsxSetSurface()`.

		This is markedly simpler than the sceGxm backend it otherwise mirrors, for a structural reason: the
		PowerVR SGX is a tile-based deferred renderer, so a pass over a surface is a *scene* with a begin and
		an end, and switching targets mid-frame means closing one and opening another. The RSX is an
		immediate-mode renderer - a surface is just a set of addresses in a state register - so switching
		targets is a state change like any other, with no lifecycle to manage and no risk of discarding what
		an earlier pass drew.

		**One colour attachment.** The RSX can bind four, but the published `MAX_COLOR_ATTACHMENTS` is 1,
		which is all the render pipeline uses (every off-screen pass here is a single-target one).

		**No depth storage of its own.** The engine is a 2D renderer that never needs a depth buffer's
		contents to survive a pass, so every target shares the one display-sized depth surface the device
		allocates rather than carrying its own. That surface is only ever scratch, and its pitch covers any
		target the pipeline creates.
	*/
	class RsxRenderTarget
	{
	public:
		/** @brief Number of colour attachments a target can carry (see the class documentation) */
		static constexpr std::uint32_t MaxColorAttachments = 1;

		RsxRenderTarget();
		~RsxRenderTarget();

		RsxRenderTarget(const RsxRenderTarget&) = delete;
		RsxRenderTarget& operator=(const RsxRenderTarget&) = delete;

		/** @brief Attaches a texture as the colour attachment with the given index */
		void AttachColorTexture(RsxTexture& texture, std::uint32_t index);
		/** @brief Detaches any texture from the colour attachment with the given index */
		void DetachColorTexture(std::uint32_t index);

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
		inline RsxTexture* GetColorTexture(std::uint32_t index) const {
			return (index < MaxColorAttachments ? _colorTextures[index] : nullptr);
		}
		/** @brief Returns the number of colour attachments enabled for drawing (see @ref SetDrawBuffers()) */
		inline std::uint32_t GetNumDrawBuffers() const {
			return _numDrawBuffers;
		}

		/**
			@brief Fills in the surface description the device programs to render into this target

			Built on demand from the attached texture's GPU-visible storage and rebuilt when the attachment
			or its size changes. The depth half is left to the caller, which fills in the shared depth
			surface. @returns `false` if the target has no usable attachment.
		*/
		bool GetDrawSurface(gcmSurface& surface, std::int32_t& width, std::int32_t& height);

	private:
		RsxTexture* _colorTextures[MaxColorAttachments];
		std::uint32_t _numDrawBuffers;

		// Description of the current attachment, rebuilt when it changes
		RsxTexture* _surfaceTexture;		// the texture the description was built over (to detect a change)
		void* _surfaceData;					// its GPU address at that point (a reallocation invalidates it)
		std::uint32_t _surfaceOffset;
		std::uint32_t _surfacePitch;
		std::int32_t _surfaceWidth;
		std::int32_t _surfaceHeight;
		bool _surfaceValid;
	};
}
