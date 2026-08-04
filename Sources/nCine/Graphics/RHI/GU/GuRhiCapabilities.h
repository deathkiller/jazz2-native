#pragma once

#include "../RhiCapabilitiesBase.h"
#include "GuDevice.h"
#include "GuRenderTarget.h"
#include "GuTexture.h"

namespace nCine::RHI::GU
{
	/**
		@brief Runtime capabilities of the GU backend

		The Allegrex GE has no queryable API context, so the limits come from the device the window backend
		has already created (see @ref RhiCapabilitiesBase::SetDeviceCapabilities()).
	*/
	class GuRhiCapabilities : public RhiCapabilitiesBase
	{
	public:
		GuRhiCapabilities()
		{
			// The GE's true texture-dimension limit (512) drives the tileset texture chunking in ContentResolver;
			// prebaked content that is larger anyway is paged internally by GuTexture rather than rejected (see
			// the note on GuDevice::GetMaxTextureDimension()). Uniforms are plain host memory decoded by the
			// draw dispatch
			SetDeviceCapabilities("GU", GuDevice::GetMaxTextureDimension(), std::int32_t(GuTexture::MaxTextureUnits),
				64 * 1024, 16, std::int32_t(GuRenderTarget::MaxColorAttachments));

			LogCapabilities();
		}
	};
}
