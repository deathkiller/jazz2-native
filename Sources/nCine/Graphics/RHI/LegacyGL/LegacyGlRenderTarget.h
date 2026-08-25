#pragma once

#include "../RhiTypes.h"

#include <cstdint>

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI::LegacyGL
{
	class LegacyGlTexture;

	/**
		@brief Renderbuffer stub of the legacy GL backend (aliased as `RHI::Renderbuffer`)

		The backend renders 2D only, so depth/stencil renderbuffers carry no storage; the class just
		records the format and size to satisfy the contract alias.
	*/
	class LegacyGlRenderbuffer
	{
	public:
		LegacyGlRenderbuffer() = default;
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
		@brief Framebuffer stub of the legacy GL backend (aliased as `RHI::Framebuffer`)

		Provided only for the contract alias (the default-framebuffer rebinding some window backends
		use). Off-screen rendering goes through @ref LegacyGlRenderTarget instead, which is what the
		pipeline actually creates.
	*/
	class LegacyGlFramebuffer
	{
	public:
		LegacyGlFramebuffer() = default;
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
		@brief Off-screen render target of the legacy GL backend (aliased as `RHI::RenderTarget`)

		Holds the color textures the rasterizer writes into (addressed by attachment index) and an
		optional depth/stencil (ignored for 2D). @ref BindDraw() records the target on the device so the
		following clears and draws land in its color attachment 0.

		Two ways of getting the pixels into that texture exist, chosen once by
		`LegacyGlDevice::SupportsFramebufferObjects()` (which is always `false` where the entry points do
		not exist at all - MiniGL on AmigaOS 4, see `RHI_LEGACYGL_HAS_FBO` in `LegacyGlApi.h`):

		- **A framebuffer object**, where one exists. Attachment 0 is the color texture itself, so the
		  draws land in it directly and nothing is ever copied.
		- **Drawing into the back buffer and copying back** (@ref IsCopyBack()), for a GL without them -
		  which includes a TinyGL that declares the entry points but does not implement them. The target
		  is rendered into the bottom-left corner of the drawable and `glCopyTexSubImage2D()` lifts the
		  region into the texture when the device leaves the target. This costs a full-surface copy per
		  pass and clobbers the back buffer, which is why it is the fallback: it is only correct because
		  every render-to-texture pass in this game runs before the screen pass that clears and redraws
		  the frame.
	*/
	class LegacyGlRenderTarget
	{
	public:
		static constexpr std::uint32_t MaxColorAttachments = 8;

		LegacyGlRenderTarget();
		~LegacyGlRenderTarget();

		LegacyGlRenderTarget(const LegacyGlRenderTarget&) = delete;
		LegacyGlRenderTarget& operator=(const LegacyGlRenderTarget&) = delete;

		/** @brief Attaches a texture as the color attachment with the given index */
		void AttachColorTexture(LegacyGlTexture& texture, std::uint32_t index);
		/** @brief Detaches any texture from the color attachment with the given index */
		void DetachColorTexture(std::uint32_t index);

		/** @brief Records a depth/stencil buffer (no storage - the backend is 2D) */
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
		inline LegacyGlTexture* GetColorTexture(std::uint32_t index) const {
			return (index < MaxColorAttachments ? _colorTextures[index] : nullptr);
		}

		/** @brief Returns the framebuffer object the draws go into, or zero in the copy-back mode */
		inline std::uint32_t GetFramebuffer() const {
			return _framebuffer;
		}
		/** @brief Returns `true` when this target is drawn into the back buffer and copied out afterwards */
		inline bool IsCopyBack() const {
			return (_framebuffer == 0 && _colorTextures[0] != nullptr);
		}
		/** @brief Copies the drawn region out of the back buffer into the color texture (copy-back only) */
		void ResolveCopyBack() const;

	private:
		LegacyGlTexture* _colorTextures[MaxColorAttachments];
		std::uint32_t _numDrawBuffers;
		/** @brief The framebuffer object, or zero when this target falls back to copying */
		std::uint32_t _framebuffer;

		/** @brief Creates (or re-points) the framebuffer object for the current attachment 0 */
		void UpdateFramebuffer();
	};
}
