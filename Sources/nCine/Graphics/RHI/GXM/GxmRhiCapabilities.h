#pragma once

#include "../RhiCapabilitiesBase.h"
#include "GxmDevice.h"
#include "GxmRenderTarget.h"
#include "GxmTexture.h"

namespace nCine::RHI::GXM
{
	/**
		@brief Runtime capabilities of the GXM backend

		sceGxm has no queryable API context, so the limits come from the device the window backend has already
		created (see @ref RhiCapabilitiesBase::SetDeviceCapabilities()).
	*/
	class GxmRhiCapabilities : public RhiCapabilitiesBase
	{
	public:
		GxmRhiCapabilities()
		{
			// Real sceGxm limits: 4096 is the SGX543's largest 2D texture dimension. A uniform block is copied into
			// the draw's default uniform buffer rather than bound as a buffer object (see GxmDevice::DrawCommon), so
			// the 64 KB block budget is the engine-side one every other backend publishes - which also keeps the
			// batch size at the value the generated Cg sources bake in - and the offset alignment is only the
			// engine's own suballocation granularity of one std140 vec4. sceGxm binds one colour surface per scene,
			// so MAX_COLOR_ATTACHMENTS is 1 (GxmRenderTarget::MaxColorAttachments).
			SetDeviceCapabilities("GXM", GxmDevice::GetMaxTextureDimension(), std::int32_t(GxmTexture::MaxTextureUnits),
				64 * 1024, GxmDevice::GetUniformBufferOffsetAlignment(), std::int32_t(GxmRenderTarget::MaxColorAttachments));

			LogCapabilities();
		}
	};
}
