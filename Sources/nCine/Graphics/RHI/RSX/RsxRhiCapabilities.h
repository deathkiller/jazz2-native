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
			// in total. What the batcher may build is bounded well below that anyway by
			// RsxDevice::MaxBatchSize, so this is a deliberately conservative byte figure rather than a
			// derivation - its only job is to keep the batcher from sizing a batch the shader cannot
			// address. The offset alignment is one std140 vec4, as elsewhere.
			// The RSX can bind four colour surfaces at once, but this backend uses one
			// (RsxRenderTarget::MaxColorAttachments) - the pipeline never asks for MRT.
			SetDeviceCapabilities("RSX", RsxDevice::GetMaxTextureDimension(), std::int32_t(RsxTexture::MaxTextureUnits),
				RsxDevice::GetMaxUniformBlockSize(), RsxDevice::GetUniformBufferOffsetAlignment(),
				std::int32_t(RsxRenderTarget::MaxColorAttachments));

			LogCapabilities();
		}
	};
}
