#pragma once

#include "../RhiCapabilitiesBase.h"
#include "SwRenderTarget.h"
#include "SwTexture.h"

namespace nCine::RHI::Software
{
	/**
		@brief Runtime capabilities of the software rasterizer backend

		The CPU rasterizer has no device to query, so it publishes the engine-side limits its own draw
		dispatch works with (see @ref RhiCapabilitiesBase::SetDeviceCapabilities()).
	*/
	class SwRhiCapabilities : public RhiCapabilitiesBase
	{
	public:
		SwRhiCapabilities()
		{
			// The CPU rasterizer samples heap-allocated texel stores addressed with 32-bit coordinates, so it has no
			// hard dimension cap of its own; 16384 is published for parity with the hardware backends (it only feeds
			// the texture-load assert). Uniforms are plain host memory read by the transpiled effects (no device
			// alignment), so 16 bytes / 64 KB are the engine-side packing granularity and the engine's normalized
			// block cap. MAX_COLOR_ATTACHMENTS mirrors SwRenderTarget::MaxColorAttachments.
			SetDeviceCapabilities("Software Rasterizer", 16384, std::int32_t(SwTexture::MaxTextureUnits),
				64 * 1024, 16, std::int32_t(SwRenderTarget::MaxColorAttachments));

			LogCapabilities();
		}
	};
}
