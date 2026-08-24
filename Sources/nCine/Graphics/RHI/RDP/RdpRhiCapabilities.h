#pragma once

#include "../RhiCapabilitiesBase.h"
#include "RdpDevice.h"
#include "RdpRenderTarget.h"
#include "RdpTexture.h"

namespace nCine::RHI::RDP
{
	/**
		@brief Runtime capabilities of the RDP backend

		The RDP has no queryable API context, so the limits come from the device the window backend has
		already created (see @ref RhiCapabilitiesBase::SetDeviceCapabilities()).
	*/
	class RdpRhiCapabilities : public RhiCapabilitiesBase
	{
	public:
		RdpRhiCapabilities()
		{
			// 1024 is what the RDP's tile descriptors can address (10.2 fixed-point SL/SH, and the
			// texture-rectangle S/T range is +-1024), and it is honest for this backend because a
			// primitive never samples the atlas as a whole - the device loads each primitive's own
			// window into TMEM. ContentResolver chunks the tileset atlases to 510x512 on this target
			// regardless (see PreferredAtlasTilesPerRow / PreferredChunkHeight), so the limit only has
			// to admit the prebaked oversized assets (the 128x529 small font atlas), which a 512 report
			// would abort on. Uniforms are plain host memory decoded by the draw dispatch.
			SetDeviceCapabilities("Nintendo 64 RDP", RdpDevice::GetMaxTextureDimension(), std::int32_t(RdpTexture::MaxTextureUnits),
				64 * 1024, 16, std::int32_t(RdpRenderTarget::MaxColorAttachments));

			LogCapabilities();
		}
	};
}
