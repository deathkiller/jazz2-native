#pragma once

#include "../RhiCapabilitiesBase.h"
#include "RsxDevice.h"
#include "RsxRenderTarget.h"
#include "RsxTexture.h"

namespace nCine::RHI::RSX
{
	/**
		@brief Runtime capabilities of the RSX backend

		libgcm has no queryable API context, so the limits come from the device the window backend has
		already created (see @ref RhiCapabilitiesBase::SetDeviceCapabilities()).
	*/
	class RsxRhiCapabilities : public RhiCapabilitiesBase
	{
	public:
		RsxRhiCapabilities()
		{
			// Real RSX limits: 4096 is the NV47's largest 2D texture dimension, and it has 16 fragment
			// texture units (the backend exposes the 8 the pipeline uses, see RsxTexture::MaxTextureUnits).
			// The uniform block budget is NOT the 64 KB the other backends publish: a block reaches the
			// vertex program through its constant registers, of which the vp40 profile allows a program 544
			// in total, so this is a deliberately conservative byte figure rather than a derivation. The
			// offset alignment is one std140 vec4, as elsewhere.
			// The block size alone cannot bound the batch here, though - it is only a byte budget, and
			// dividing it by an instance stride yields more instances than the microcode can address (65 at
			// a stride of 112 against the 32 every batched variant is compiled for). MaxBatchSize is what
			// actually caps it, and the batcher has to be told rather than left to derive it.
			// The RSX can bind four colour surfaces at once, but this backend uses one
			// (RsxRenderTarget::MaxColorAttachments) - the pipeline never asks for MRT.
			SetDeviceCapabilities("RSX", RsxDevice::GetMaxTextureDimension(), std::int32_t(RsxTexture::MaxTextureUnits),
				RsxDevice::GetMaxUniformBlockSize(), RsxDevice::GetUniformBufferOffsetAlignment(),
				std::int32_t(RsxRenderTarget::MaxColorAttachments), std::int32_t(RsxDevice::MaxBatchSize));

			LogCapabilities();
		}
	};
}
