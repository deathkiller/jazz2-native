#pragma once

// Backend-INTERNAL header of the Vulkan RHI. Unlike the contract headers (VulkanDevice.h,
// VulkanTexture.h, ...) — which stay free of <vulkan/vulkan.h> because they are pulled in across the whole
// render pipeline through Rhi.h — this header pulls in the Vulkan headers and the dynamically-resolved loader
// entry points, and is included ONLY by the Vulkan backend translation units (VulkanDevice.cpp,
// VulkanShaderProgram.cpp, VulkanBufferObject.cpp, VulkanTexture.cpp, VulkanRenderTarget.cpp). The backend
// needs the Vulkan symbols in several .cpp to build the real GPU resources, so the loader is exposed here
// (the whole file only ever compiles in a WITH_RHI_VULKAN build).

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <cstdint>

namespace nCine::RHI::Vulkan
{
	// -- Dynamically-loaded Vulkan entry points --
	// Defined in VulkanDevice.cpp and resolved through vkGetInstanceProcAddr / vkGetDeviceProcAddr (no
	// vulkan-1.lib is linked; the loader is bootstrapped from SDL). Each shares the API name (legal under
	// VK_NO_PROTOTYPES, since vulkan.h declares no real prototypes) and lives at namespace scope so every
	// backend translation unit can call it. The declaration list mirrors the loader in VulkanDevice.cpp.
	extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;

	extern PFN_vkCreateInstance vkCreateInstance;
	extern PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties;
	extern PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties;

	extern PFN_vkDestroyInstance vkDestroyInstance;
	extern PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
	extern PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
	extern PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures;
	extern PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
	extern PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties;
	extern PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
	extern PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties;
	extern PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
	extern PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
	extern PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;
	extern PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR;
	extern PFN_vkCreateDevice vkCreateDevice;
	extern PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
	extern PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;
	extern PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT;
	extern PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT;

	extern PFN_vkDestroyDevice vkDestroyDevice;
	extern PFN_vkDeviceWaitIdle vkDeviceWaitIdle;
	extern PFN_vkGetDeviceQueue vkGetDeviceQueue;
	extern PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
	extern PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR;
	extern PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;
	extern PFN_vkCreateImageView vkCreateImageView;
	extern PFN_vkDestroyImageView vkDestroyImageView;
	extern PFN_vkCreateCommandPool vkCreateCommandPool;
	extern PFN_vkDestroyCommandPool vkDestroyCommandPool;
	extern PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
	extern PFN_vkFreeCommandBuffers vkFreeCommandBuffers;
	extern PFN_vkResetCommandBuffer vkResetCommandBuffer;
	extern PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
	extern PFN_vkEndCommandBuffer vkEndCommandBuffer;
	extern PFN_vkCmdClearColorImage vkCmdClearColorImage;
	extern PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
	extern PFN_vkCreateSemaphore vkCreateSemaphore;
	extern PFN_vkDestroySemaphore vkDestroySemaphore;
	extern PFN_vkCreateFence vkCreateFence;
	extern PFN_vkDestroyFence vkDestroyFence;
	extern PFN_vkWaitForFences vkWaitForFences;
	extern PFN_vkResetFences vkResetFences;
	extern PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR;
	extern PFN_vkQueueSubmit vkQueueSubmit;
	extern PFN_vkQueuePresentKHR vkQueuePresentKHR;
	extern PFN_vkQueueWaitIdle vkQueueWaitIdle;

	// -- Real GPU resources / pipelines / draws --
	extern PFN_vkCreateBuffer vkCreateBuffer;
	extern PFN_vkDestroyBuffer vkDestroyBuffer;
	extern PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
	extern PFN_vkBindBufferMemory vkBindBufferMemory;
	extern PFN_vkCreateImage vkCreateImage;
	extern PFN_vkDestroyImage vkDestroyImage;
	extern PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements;
	extern PFN_vkBindImageMemory vkBindImageMemory;
	extern PFN_vkAllocateMemory vkAllocateMemory;
	extern PFN_vkFreeMemory vkFreeMemory;
	extern PFN_vkMapMemory vkMapMemory;
	extern PFN_vkUnmapMemory vkUnmapMemory;
	extern PFN_vkCreateSampler vkCreateSampler;
	extern PFN_vkDestroySampler vkDestroySampler;
	extern PFN_vkCreateShaderModule vkCreateShaderModule;
	extern PFN_vkDestroyShaderModule vkDestroyShaderModule;
	extern PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout;
	extern PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout;
	extern PFN_vkCreateDescriptorPool vkCreateDescriptorPool;
	extern PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool;
	extern PFN_vkResetDescriptorPool vkResetDescriptorPool;
	extern PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets;
	extern PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets;
	extern PFN_vkCreatePipelineLayout vkCreatePipelineLayout;
	extern PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout;
	extern PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines;
	extern PFN_vkDestroyPipeline vkDestroyPipeline;
	extern PFN_vkCreatePipelineCache vkCreatePipelineCache;
	extern PFN_vkDestroyPipelineCache vkDestroyPipelineCache;
	extern PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer;
	extern PFN_vkCreateRenderPass vkCreateRenderPass;
	extern PFN_vkDestroyRenderPass vkDestroyRenderPass;
	extern PFN_vkCreateFramebuffer vkCreateFramebuffer;
	extern PFN_vkDestroyFramebuffer vkDestroyFramebuffer;
	extern PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass;
	extern PFN_vkCmdEndRenderPass vkCmdEndRenderPass;
	extern PFN_vkCmdBindPipeline vkCmdBindPipeline;
	extern PFN_vkCmdBindVertexBuffers vkCmdBindVertexBuffers;
	extern PFN_vkCmdBindIndexBuffer vkCmdBindIndexBuffer;
	extern PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets;
	extern PFN_vkCmdSetViewport vkCmdSetViewport;
	extern PFN_vkCmdSetScissor vkCmdSetScissor;
	extern PFN_vkCmdDraw vkCmdDraw;
	extern PFN_vkCmdDrawIndexed vkCmdDrawIndexed;
	extern PFN_vkCmdCopyBuffer vkCmdCopyBuffer;
	extern PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage;
	extern PFN_vkCmdClearAttachments vkCmdClearAttachments;
	extern PFN_vkCmdBlitImage vkCmdBlitImage;

	// -- Shared device context accessors (defined in VulkanDevice.cpp) --

	/** @brief The logical device, or `VK_NULL_HANDLE` before creation / after teardown */
	VkDevice VkDeviceHandle();
	/** @brief The selected physical device */
	VkPhysicalDevice VkPhysicalDeviceHandle();
	/** @brief Returns a memory type index satisfying @p typeBits with all @p properties, or `UINT32_MAX` if none */
	std::uint32_t VkFindMemoryType(std::uint32_t typeBits, VkMemoryPropertyFlags properties);
	/** @brief The device's minimum uniform-buffer bind-offset alignment (for the per-frame uniform ring) */
	VkDeviceSize VkMinUniformBufferOffsetAlignment();

	/**
		@brief Allocates + begins a one-time-submit primary command buffer from the shared graphics pool

		Used by the resource classes for out-of-frame uploads / layout transitions (texture staging copies).
		Pair with @ref VkEndOneTimeCommands(), which submits it and waits for the graphics queue to go idle.
		Returns `VK_NULL_HANDLE` if the device is not ready.
	*/
	VkCommandBuffer VkBeginOneTimeCommands();
	/** @brief Ends, submits and waits on a command buffer from @ref VkBeginOneTimeCommands(), then frees it */
	void VkEndOneTimeCommands(VkCommandBuffer commandBuffer);

	/**
		@brief Hands a texture-upload staging buffer/memory to the current in-flight frame for deferred destruction

		Used by @ref VulkanTexture::RecordStreamingUpload(), which records the staging copy into the frame's
		command buffer instead of a queue-idle one-time submit. The buffer/memory (opaque `VkBuffer` / `VkDeviceMemory`
		as `std::uint64_t`) are freed the next time that frame slot is recorded, once its fence has signalled.
	*/
	void VkRegisterFrameStaging(std::uint64_t buffer, std::uint64_t memory);

	// -- Deferred resource destruction --
	// A GPU resource whose C++ owner (VulkanTexture / VulkanBufferObject / VulkanRenderTarget) is destroyed - or
	// which is replaced on a re-allocation / grow / view rebuild - may still be referenced by an in-flight frame's
	// command buffer or descriptor set (MaxFramesInFlight frames run concurrently; BeginFrame waits only the
	// current slot's fence). Freeing it immediately is a GPU use-after-free that loses the device. Instead the
	// resource classes ENQUEUE their handles here and the device frees each only once every frame that could
	// reference it has completed - a full MaxFramesInFlight fence cycle later. Mirrors the staging-buffer deferral
	// (@ref VkRegisterFrameStaging), the only other place a resource must outlive the frame that recorded it.

	/** @brief Kind of Vulkan handle handed to @ref VkEnqueueDestroy() (selects the right `vkDestroy*` / `vkFreeMemory`) */
	enum class VkDeferredResource : std::uint32_t
	{
		Image,
		ImageView,
		Sampler,
		Buffer,
		DeviceMemory,
		Framebuffer
	};

	/**
		@brief Enqueues a GPU resource handle for destruction once no in-flight frame can still reference it

		The handle (an opaque `Vk*` cast to `std::uint64_t`) is freed by the device a full `MaxFramesInFlight` fence
		cycle later (see the deferred-destroy tick in `BeginFrame`), guaranteeing every command buffer that could
		reference it has completed first. A zero handle is ignored. If the device is already gone
		(`VK_NULL_HANDLE`, teardown) the call is a no-op: `vkDestroyDevice` implicitly destroys every child object,
		so nothing is (or can be) freed and nothing is in flight.
	*/
	void VkEnqueueDestroy(VkDeferredResource type, std::uint64_t handle);

	/** @brief Returns (creating/caching on first use) a single-color-attachment render pass for @p format (loadOp LOAD, storeOp STORE, layout COLOR_ATTACHMENT_OPTIMAL) */
	VkRenderPass VkGetColorRenderPass(VkFormat format);

	/**
		@brief Returns (creating/caching on first use) a color-only render pass over @p count attachments with the given formats

		The multi-render-target generalization of @ref VkGetColorRenderPass(): the cache is keyed by the full
		attachment-format signature (count + formats), every attachment uses loadOp LOAD / storeOp STORE and
		stays in COLOR_ATTACHMENT_OPTIMAL, and the single subpass writes all @p count attachments. Returns
		`VK_NULL_HANDLE` if @p count is 0 or exceeds the render-target contract's attachment limit.
	*/
	VkRenderPass VkGetColorRenderPassMrt(const VkFormat* formats, std::uint32_t count);
}
