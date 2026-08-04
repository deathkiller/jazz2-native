#pragma once

#include "../RhiCapabilitiesBase.h"
#include "GxDevice.h"
#include "GxRenderTarget.h"
#include "GxTexture.h"

namespace nCine::RHI::GX
{
	/**
		@brief Runtime capabilities of the GX backend

		GX has no queryable API context, so the limits come from the device the window backend has already
		created (see @ref RhiCapabilitiesBase::SetDeviceCapabilities()).
	*/
	class GxRhiCapabilities : public RhiCapabilitiesBase
	{
	public:
		GxRhiCapabilities()
		{
			// The Flipper/Hollywood texture-dimension limit (1024) is the value that drives the tileset
			// texture chunking in ContentResolver; uniforms are plain host memory decoded by the draw dispatch
			// (no device alignment), so 16 bytes / 64 KB are the engine-side packing granularity
			SetDeviceCapabilities("GX", GxDevice::GetMaxTextureDimension(), std::int32_t(GxTexture::MaxTextureUnits),
				64 * 1024, 16, std::int32_t(GxRenderTarget::MaxColorAttachments));

			LogCapabilities();
		}
	};
}
