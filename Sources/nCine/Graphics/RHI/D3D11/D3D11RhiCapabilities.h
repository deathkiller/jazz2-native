#pragma once

#include "../RhiCapabilitiesBase.h"
#include "D3D11Device.h"
#include "D3D11RenderTarget.h"
#include "D3D11Texture.h"

namespace nCine::RHI::D3D11
{
	/**
		@brief Runtime capabilities of the Direct3D 11 backend

		Direct3D has no queryable API context of the OpenGL kind, so the limits come from the device the window
		backend has already created (see @ref RhiCapabilitiesBase::SetDeviceCapabilities()).
	*/
	class D3D11RhiCapabilities : public RhiCapabilitiesBase
	{
	public:
		D3D11RhiCapabilities()
		{
			// Real Direct3D 11 limits: the texture dimension comes from the obtained feature level (16384 on 11_0,
			// 8192 on the 10.x fallbacks); a constant buffer holds 4096 16-byte constants = 64 KB
			// (D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT). The backend copies every bound uniform range into a pooled
			// cbuffer per draw (offsets never reach the API), so the offset alignment is only the engine-side
			// suballocation granularity: 16 bytes, one std140 vec4. MAX_COLOR_ATTACHMENTS is
			// D3D11RenderTarget::MaxColorAttachments, which is D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT (8).
			SetDeviceCapabilities("Direct3D 11", D3D11Device::GetMaxTextureDimension(), std::int32_t(D3D11Texture::MaxTextureUnits),
				64 * 1024, 16, std::int32_t(D3D11RenderTarget::MaxColorAttachments));

			LogCapabilities();
		}
	};
}
