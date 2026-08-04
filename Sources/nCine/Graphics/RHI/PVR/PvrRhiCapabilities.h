#pragma once

#include "../RhiCapabilitiesBase.h"
#include "PvrDevice.h"
#include "PvrRenderTarget.h"
#include "PvrTexture.h"

namespace nCine::RHI::PVR
{
	/**
		@brief Runtime capabilities of the PowerVR backend

		The tile accelerator has no queryable API context, so the limits come from the device the window
		backend has already created (see @ref RhiCapabilitiesBase::SetDeviceCapabilities()).
	*/
	class PvrRhiCapabilities : public RhiCapabilitiesBase
	{
	public:
		PvrRhiCapabilities()
		{
			// The PowerVR CLX2 texture-dimension limit (1024) drives the tileset texture chunking in
			// ContentResolver; uniforms are plain host memory decoded by the draw dispatch
			SetDeviceCapabilities("PowerVR", PvrDevice::GetMaxTextureDimension(), std::int32_t(PvrTexture::MaxTextureUnits),
				64 * 1024, 16, std::int32_t(PvrRenderTarget::MaxColorAttachments));

			LogCapabilities();
		}
	};
}
