#include "VulkanRenderTarget.h"
#include "VulkanDevice.h"
#include "VulkanTexture.h"
#include "VulkanCommon.h"

namespace nCine::RhiVulkan
{
	VulkanRenderTarget::VulkanRenderTarget()
		: numDrawBuffers_(1), framebuffer_(0), framebufferView_(0)
	{
		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			colorTextures_[i] = nullptr;
		}
	}

	VulkanRenderTarget::~VulkanRenderTarget()
	{
		// Clear from the device first so a destroyed render target can't dangle as the current one
		VulkanDevice::UnbindRenderTarget(this);
		ReleaseFramebuffer();
	}

	void VulkanRenderTarget::ReleaseFramebuffer()
	{
		// Defer the framebuffer free: an in-flight frame's command buffer may still have a render pass begun on it
		// (up to MaxFramesInFlight frames run concurrently), and this also runs on an attachment rebuild; freeing
		// now would be a GPU use-after-free. The device frees it once every referencing frame has completed.
		// VkEnqueueDestroy no-ops if the device is already gone (teardown).
		VkEnqueueDestroy(VkDeferredResource::Framebuffer, framebuffer_);
		framebuffer_ = 0;
		framebufferView_ = 0;
	}

	std::uint64_t VulkanRenderTarget::GetFramebuffer()
	{
		VulkanTexture* color = colorTextures_[0];
		VkDevice device = VkDeviceHandle();
		if (color == nullptr || device == VK_NULL_HANDLE || color->GetWidth() <= 0 || color->GetHeight() <= 0) {
			return 0;
		}

		// Ensure the attachment image / view exist, then (re)build the framebuffer if the view changed
		const std::uint64_t view = color->GpuView();
		if (view == 0) {
			return 0;
		}
		if (framebuffer_ != 0 && framebufferView_ == view) {
			return framebuffer_;
		}
		ReleaseFramebuffer();

		VkImageView attachment = reinterpret_cast<VkImageView>(view);
		VkFramebufferCreateInfo fci = {};
		fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fci.renderPass = VkGetColorRenderPass(VkFormat(color->GpuFormat()));
		fci.attachmentCount = 1;
		fci.pAttachments = &attachment;
		fci.width = std::uint32_t(color->GetWidth());
		fci.height = std::uint32_t(color->GetHeight());
		fci.layers = 1;
		VkFramebuffer framebuffer = VK_NULL_HANDLE;
		if (vkCreateFramebuffer(device, &fci, nullptr, &framebuffer) != VK_SUCCESS) {
			return 0;
		}
		framebuffer_ = reinterpret_cast<std::uint64_t>(framebuffer);
		framebufferView_ = view;
		return framebuffer_;
	}

	void VulkanRenderTarget::AttachColorTexture(VulkanTexture& texture, std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			colorTextures_[index] = &texture;
			texture.SetRenderTarget(true);
			if (index == 0) {
				ReleaseFramebuffer();
			}
		}
	}

	void VulkanRenderTarget::DetachColorTexture(std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			colorTextures_[index] = nullptr;
			if (index == 0) {
				ReleaseFramebuffer();
			}
		}
	}

	void VulkanRenderTarget::AttachDepthStencil(DepthStencilFormat format, std::int32_t width, std::int32_t height)
	{
		static_cast<void>(format);
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void VulkanRenderTarget::DetachDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void VulkanRenderTarget::BindDraw()
	{
		VulkanDevice::SetRenderTarget(this);
	}

	void VulkanRenderTarget::UnbindDraw()
	{
		VulkanDevice::SetRenderTarget(nullptr);
	}

	bool VulkanRenderTarget::SetDrawBuffers(std::uint32_t numColorAttachments)
	{
		numDrawBuffers_ = numColorAttachments;
		return true;
	}

	bool VulkanRenderTarget::IsStatusComplete()
	{
		return (colorTextures_[0] != nullptr);
	}

	void VulkanRenderTarget::InvalidateDepthStencil(DepthStencilFormat format)
	{
		static_cast<void>(format);
	}

	void VulkanRenderTarget::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
