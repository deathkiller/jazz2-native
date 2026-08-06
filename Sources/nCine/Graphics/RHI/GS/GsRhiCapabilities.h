#pragma once

#include "../RhiCapabilitiesBase.h"
#include "GsDevice.h"
#include "GsRenderTarget.h"
#include "GsTexture.h"

namespace nCine::RHI::GS
{
	/**
		@brief Runtime capabilities of the Graphics Synthesizer backend

		The GS has no queryable API context at all - it is a set of memory-mapped registers - so the limits
		are the hardware's own, stated here (see @ref RhiCapabilitiesBase::SetDeviceCapabilities()).
	*/
	class GsRhiCapabilities : public RhiCapabilitiesBase
	{
	public:
		GsRhiCapabilities()
		{
			// `TEX0.TW`/`TH` are log2 sizes capped at 10, so 1024 is the hardware texture-dimension limit -
			// the same as the PowerVR's, and it drives the tileset texture chunking in ContentResolver.
			// Uniforms are plain host memory decoded by the draw dispatch.
			SetDeviceCapabilities("Graphics Synthesizer", GsDevice::GetMaxTextureDimension(), std::int32_t(GsTexture::MaxTextureUnits),
				64 * 1024, 16, std::int32_t(GsRenderTarget::MaxColorAttachments));

			LogCapabilities();
		}
	};
}
