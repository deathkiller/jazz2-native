#include "VulkanRenderTarget.h"
#include "VulkanDevice.h"
#include "VulkanTexture.h"
#include "VulkanCommon.h"

#include <cstring>

namespace nCine::RHI::Vulkan
{
	VulkanRenderTarget::VulkanRenderTarget()
		: _numDrawBuffers(1), _framebuffer(0), _framebufferViewCount(0)
	{
		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			_colorTextures[i] = nullptr;
			_framebufferViews[i] = 0;
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
		VkEnqueueDestroy(VkDeferredResource::Framebuffer, _framebuffer);
		_framebuffer = 0;
		for (std::uint32_t i = 0; i < MaxColorAttachments; i++) {
			_framebufferViews[i] = 0;
		}
		_framebufferViewCount = 0;
	}

	std::uint32_t VulkanRenderTarget::GetAttachedCount() const
	{
		std::uint32_t count = 0;
		while (count < MaxColorAttachments && _colorTextures[count] != nullptr) {
			count++;
		}
		return count;
	}

	std::uint64_t VulkanRenderTarget::GetFramebuffer()
	{
		VulkanTexture* color = _colorTextures[0];
		VkDevice device = VkDeviceHandle();
		if (color == nullptr || device == VK_NULL_HANDLE || color->GetWidth() <= 0 || color->GetHeight() <= 0) {
			return 0;
		}

		// The framebuffer spans every contiguously attached color texture (attachment 0..count-1); ensure each
		// attachment's image / view exist, then (re)build the framebuffer if any view (or the count) changed
		const std::uint32_t count = GetAttachedCount();
		std::uint64_t views[MaxColorAttachments];
		VkImageView attachments[MaxColorAttachments];
		VkFormat formats[MaxColorAttachments];
		for (std::uint32_t i = 0; i < count; i++) {
			views[i] = _colorTextures[i]->GpuView();
			if (views[i] == 0) {
				return 0;
			}
			attachments[i] = reinterpret_cast<VkImageView>(views[i]);
			formats[i] = VkFormat(_colorTextures[i]->GpuFormat());
		}
		if (_framebuffer != 0 && _framebufferViewCount == count &&
			std::memcmp(_framebufferViews, views, count * sizeof(std::uint64_t)) == 0) {
			return _framebuffer;
		}
		ReleaseFramebuffer();

		VkRenderPass renderPass = VkGetColorRenderPassMrt(formats, count);
		if (renderPass == VK_NULL_HANDLE) {
			return 0;
		}
		VkFramebufferCreateInfo fci = {};
		fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fci.renderPass = renderPass;
		fci.attachmentCount = count;
		fci.pAttachments = attachments;
		fci.width = std::uint32_t(color->GetWidth());
		fci.height = std::uint32_t(color->GetHeight());
		fci.layers = 1;
		VkFramebuffer framebuffer = VK_NULL_HANDLE;
		if (vkCreateFramebuffer(device, &fci, nullptr, &framebuffer) != VK_SUCCESS) {
			return 0;
		}
		_framebuffer = reinterpret_cast<std::uint64_t>(framebuffer);
		for (std::uint32_t i = 0; i < count; i++) {
			_framebufferViews[i] = views[i];
		}
		_framebufferViewCount = count;
		return _framebuffer;
	}

	void VulkanRenderTarget::AttachColorTexture(VulkanTexture& texture, std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			_colorTextures[index] = &texture;
			texture.SetRenderTarget(true);
			// Any attachment is (potentially) part of the framebuffer now, so a change to any slot rebuilds it
			ReleaseFramebuffer();
		}
	}

	void VulkanRenderTarget::DetachColorTexture(std::uint32_t index)
	{
		if (index < MaxColorAttachments) {
			_colorTextures[index] = nullptr;
			ReleaseFramebuffer();
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
		_numDrawBuffers = numColorAttachments;
		return true;
	}

	bool VulkanRenderTarget::IsStatusComplete()
	{
		return (_colorTextures[0] != nullptr);
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
