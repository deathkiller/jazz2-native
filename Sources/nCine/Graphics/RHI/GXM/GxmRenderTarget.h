#pragma once

#include "../RhiTypes.h"

#include <cstdint>

#include <Containers/StringView.h>

#include <psp2/gxm.h>

using namespace Death::Containers;

namespace nCine::RHI::GXM
{
	class GxmTexture;

	/**
		@brief Renderbuffer stub of the sceGxm backend (aliased as `RHI::Renderbuffer`)

		Carries no depth/stencil storage of its own (the renderer is 2D, and the backend shares one
		depth/stencil surface across every scene - see @ref GxmRenderTarget); the class only records the
		format and size to satisfy the contract alias.
	*/
	class GxmRenderbuffer
	{
	public:
		GxmRenderbuffer() = default;
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
		@brief Framebuffer stub of the sceGxm backend (aliased as `RHI::Framebuffer`)

		Provided only for the contract alias (the default-framebuffer rebinding some window backends use).
		Off-screen rendering is routed through @ref GxmRenderTarget instead.
	*/
	class GxmFramebuffer
	{
	public:
		GxmFramebuffer() = default;
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
		@brief Off-screen render target of the sceGxm backend (aliased as `RHI::RenderTarget`)

		Records the colour texture the following scene renders into and owns the `SceGxmRenderTarget` that
		describes how that surface is tiled. @ref BindDraw() records the target on the device, which closes
		the scene in progress and opens a new one over this surface (sceGxm has no "switch the framebuffer"
		operation - a scene *is* a pass over one surface).

		**One colour attachment.** sceGxm binds a single colour surface per scene, so unlike the desktop
		backends there is no multi-attachment case to bound: the published `MAX_COLOR_ATTACHMENTS` is 1,
		which is also all the render pipeline uses (every off-screen pass here is a single-target one).

		**No depth storage.** The engine is a 2D renderer that never needs a depth buffer's contents to
		survive a pass, and a tile-based architecture gives depth testing *within* a scene from the on-chip
		tile buffer for free. Every scene therefore shares the one panel-sized depth/stencil surface the
		device allocates, rather than each target carrying its own - the surface is only ever a scratch area,
		and its stride covers any target the pipeline creates.
	*/
	class GxmRenderTarget
	{
	public:
		/** @brief Number of colour attachments a target can carry (see the class documentation) */
		static constexpr std::uint32_t MaxColorAttachments = 1;

		GxmRenderTarget();
		~GxmRenderTarget();

		GxmRenderTarget(const GxmRenderTarget&) = delete;
		GxmRenderTarget& operator=(const GxmRenderTarget&) = delete;

		/** @brief Attaches a texture as the colour attachment with the given index */
		void AttachColorTexture(GxmTexture& texture, std::uint32_t index);
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
		inline GxmTexture* GetColorTexture(std::uint32_t index) const {
			return (index < MaxColorAttachments ? colorTextures_[index] : nullptr);
		}
		/** @brief Returns the number of colour attachments enabled for drawing (see @ref SetDrawBuffers()) */
		inline std::uint32_t GetNumDrawBuffers() const {
			return numDrawBuffers_;
		}

		/**
			@brief Returns the sceGxm render target, colour surface and sync object a scene over this target begins with

			All three are built on demand from the attached texture's GPU-visible surface, and rebuilt when the
			attachment or its size changes. @returns `false` if the target has no usable attachment.

			The sync object is what serializes operations on these texels against each other. sceGxm pipelines
			scenes, so a scene that renders into a surface and a later one that samples it are not otherwise
			ordered - which is the whole reason `sceGxmBeginScene()` takes one, and passing `nullptr` (as this
			backend did) leaves a render-to-texture hand-off with nothing guaranteeing the write finished before
            the read. The display buffers have always had theirs for the same reason, against the scan-out.
		*/
		bool GetSceneTarget(SceGxmRenderTarget*& renderTarget, SceGxmColorSurface*& colorSurface,
			SceGxmSyncObject*& syncObject, std::int32_t& width, std::int32_t& height);

	private:
		GxmTexture* colorTextures_[MaxColorAttachments];
		std::uint32_t numDrawBuffers_;

		// The sceGxm objects describing the current attachment, rebuilt when it changes
		SceGxmRenderTarget* gxmRenderTarget_;
		SceGxmColorSurface colorSurface_;
		SceGxmSyncObject* syncObject_;		// serializes writing these texels against sampling them
		GxmTexture* surfaceTexture_;		// the texture colorSurface_ was built over (to detect a change)
		void* surfaceData_;					// its GPU address at that point (a reallocation invalidates the surface)
		std::int32_t surfaceWidth_;
		std::int32_t surfaceHeight_;
		bool surfaceValid_;

		/** @brief Releases the sceGxm render target and invalidates the colour surface */
		void ReleaseSceneTarget();
	};
}
