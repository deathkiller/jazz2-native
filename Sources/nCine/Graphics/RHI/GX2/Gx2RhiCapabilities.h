#pragma once

#include "../RhiCapabilitiesBase.h"
#include "Gx2Device.h"
#include "Gx2RenderTarget.h"
#include "Gx2Texture.h"

namespace nCine::RHI::GX2
{
	/**
		@brief Runtime capabilities of the GX2 backend

		GX2 has no queryable API context, so the limits come from the device the window backend has already
		created (see @ref RhiCapabilitiesBase::SetDeviceCapabilities()).
	*/
	class Gx2RhiCapabilities : public RhiCapabilitiesBase
	{
	public:
		Gx2RhiCapabilities()
		{
			// Latte limits: 8192 is its largest 2D texture dimension and it has 16 pixel shader texture units
			// (the backend exposes the 8 the pipeline uses, see Gx2Texture::MaxTextureUnits). A uniform block
			// may hold 64 KB; the offset alignment is GX2_UNIFORM_BLOCK_ALIGNMENT (256 bytes). MaxBatchSize is
			// what fits a 64 KB block at the sprite instance stride (see Gx2Device::MaxBatchSize).
			SetDeviceCapabilities("GX2", Gx2Device::GetMaxTextureDimension(), std::int32_t(Gx2Texture::MaxTextureUnits),
				Gx2Device::GetMaxUniformBlockSize(), Gx2Device::GetUniformBufferOffsetAlignment(),
				std::int32_t(Gx2RenderTarget::MaxColorAttachments), std::int32_t(Gx2Device::MaxBatchSize));

			LogCapabilities();
		}
	};
}
