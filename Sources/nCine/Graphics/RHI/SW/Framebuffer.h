#pragma once

#if defined(WITH_RHI_SW)

#include "../RenderTypes.h"
#include "Texture.h"

#include <cstdint>

namespace nCine::RHI
{
	// =========================================================================
	// Renderbuffer — placeholder for depth/stencil (not used in SW)
	// =========================================================================
	class Renderbuffer
	{
	public:
		Renderbuffer() = default;
		void Storage(RenderbufferFormat /*format*/, std::int32_t width, std::int32_t height)
		{
			width_  = width;
			height_ = height;
		}
		std::int32_t GetWidth()  const { return width_;  }
		std::int32_t GetHeight() const { return height_; }
	private:
		std::int32_t width_  = 0;
		std::int32_t height_ = 0;
	};

	// =========================================================================
	// Framebuffer — CPU-side RGBA8 render target
	// =========================================================================
	class Framebuffer
	{
	public:
		Framebuffer() = default;
		void AttachTexture(FramebufferAttachment /*slot*/, Texture* texture) { colorTarget_ = texture; }
		void AttachRenderbuffer(FramebufferAttachment /*slot*/, Renderbuffer* /*rb*/) {}
		bool IsComplete() const { return colorTarget_ != nullptr; }
		Texture* GetColorTarget() { return colorTarget_; }
	private:
		Texture* colorTarget_ = nullptr;
	};
}

#endif
