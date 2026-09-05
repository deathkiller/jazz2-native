#pragma once

#include "../RhiCapabilitiesBase.h"
#include "PicaDevice.h"
#include "PicaRenderTarget.h"
#include "PicaTexture.h"

namespace nCine::RHI::PICA
{
	/**
		@brief Runtime capabilities of the PICA backend

		The PICA200 has no queryable API context, so the limits come from the device the window backend
		has already created (see @ref RhiCapabilitiesBase::SetDeviceCapabilities()).
	*/
	class PicaRhiCapabilities : public RhiCapabilitiesBase
	{
	public:
		PicaRhiCapabilities()
		{
			// The GPU's true texture-dimension limit (1024) drives the tileset texture chunking in ContentResolver;
			// prebaked content that is larger anyway is paged internally by PicaTexture rather than rejected (see
			// the note on PicaDevice::GetMaxTextureDimension()). Uniforms are plain host memory decoded by the
			// draw dispatch
			SetDeviceCapabilities("PICA200", PicaDevice::GetMaxTextureDimension(), std::int32_t(PicaTexture::MaxTextureUnits),
				64 * 1024, 16, std::int32_t(PicaRenderTarget::MaxColorAttachments));

			LogCapabilities();
		}
	};
}
