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
			// The batch size is stated rather than derived. A batched draw's instance array lives in the
			// draw's *default uniform buffer*, which is uploaded per draw and indexed dynamically by the
			// shader; sized from the 64 KB block budget above that array is 585 instances, i.e. 64 KB of
			// per-draw uniform data for a PowerVR SGX543, which is not a shape this hardware is meant to be
			// fed. The engine already forces a fixed batch on the PowerVR Rogue parts for the same reason
			// (see Application::InitCommon()), one generation *newer* than the Vita's, so the same 10 is used.
			SetDeviceCapabilities("GXM", GxmDevice::GetMaxTextureDimension(), std::int32_t(GxmTexture::MaxTextureUnits),
				64 * 1024, GxmDevice::GetUniformBufferOffsetAlignment(), std::int32_t(GxmRenderTarget::MaxColorAttachments), 10);

			LogCapabilities();
		}
	};
}
