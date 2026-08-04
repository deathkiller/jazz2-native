#pragma once

#include "../RhiCapabilitiesBase.h"
#include "VulkanDevice.h"
#include "VulkanRenderTarget.h"
#include "VulkanTexture.h"

namespace nCine::RHI::Vulkan
{
	/**
		@brief Runtime capabilities of the Vulkan backend

		Vulkan has no queryable API context of the OpenGL kind, so the limits come from the physical device the
		window backend has already selected (see @ref RhiCapabilitiesBase::SetDeviceCapabilities()).
	*/
	class VulkanRhiCapabilities : public RhiCapabilitiesBase
	{
	public:
		VulkanRhiCapabilities()
		{
			// Real limits of the selected VkPhysicalDevice, queried at device creation: maxImageDimension2D,
			// maxUniformBufferRange (normalized into the [16 KB, 64 KB] window exactly like the OpenGL path) and
			// minUniformBufferOffsetAlignment. The backend re-aligns its own per-frame uniform ring at bind time
			// (see AllocUbo), so the engine-side suballocation does not strictly need the device alignment - it
			// follows it anyway so the published value is the real one. The render passes drive a single color
			// attachment today (MRT unimplemented); VulkanRenderTarget::MaxColorAttachments mirrors the common
			// device limit and only bounds Viewport::SetTexture().
			SetDeviceCapabilities("Vulkan", VulkanDevice::GetMaxTextureDimension(), std::int32_t(VulkanTexture::MaxTextureUnits),
				VulkanDevice::GetMaxUniformBufferRange(), VulkanDevice::GetUniformBufferOffsetAlignment(),
				std::int32_t(VulkanRenderTarget::MaxColorAttachments));

			LogCapabilities();
		}
	};
}
