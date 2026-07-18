#include "VulkanDevice.h"
#include "VulkanShaderProgram.h"
#include "VulkanRenderTarget.h"
#include "VulkanTexture.h"
#include "VulkanBufferObject.h"
#include "VulkanCommon.h"

#include <cstdint>
#include <cstring>
#include <vector>
#include <unordered_map>

// vulkan.h comes from VulkanCommon.h (with VK_NO_PROTOTYPES). SDL_vulkan.h is included only here (the loader
// bootstrap + surface creation live in this translation unit); it must see the real Vulkan handle types.
#if defined(WITH_SDL)
#	if !defined(CMAKE_BUILD) && defined(__has_include)
#		if __has_include("SDL2/SDL_vulkan.h")
#			define __HAS_LOCAL_SDL_VULKAN
#		endif
#	endif
#	if defined(__HAS_LOCAL_SDL_VULKAN)
#		include "SDL2/SDL.h"
#		include "SDL2/SDL_vulkan.h"
#	else
#		include <SDL.h>
#		include <SDL_vulkan.h>
#	endif
#endif

#include <Asserts.h>

namespace nCine::RhiVulkan
{
	// -- Loader entry points (declared extern in VulkanCommon.h; resolved through vkGetInstanceProcAddr /
	// vkGetDeviceProcAddr in the Load*Functions() below). Defined here at namespace scope so every backend
	// translation unit shares them. --
	PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;

	PFN_vkCreateInstance vkCreateInstance = nullptr;
	PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties = nullptr;
	PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties = nullptr;

	PFN_vkDestroyInstance vkDestroyInstance = nullptr;
	PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = nullptr;
	PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = nullptr;
	PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures = nullptr;
	PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties = nullptr;
	PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties = nullptr;
	PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties = nullptr;
	PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties = nullptr;
	PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR = nullptr;
	PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
	PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR = nullptr;
	PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR = nullptr;
	PFN_vkCreateDevice vkCreateDevice = nullptr;
	PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;
	PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR = nullptr;
	PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = nullptr;
	PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT = nullptr;

	PFN_vkDestroyDevice vkDestroyDevice = nullptr;
	PFN_vkDeviceWaitIdle vkDeviceWaitIdle = nullptr;
	PFN_vkGetDeviceQueue vkGetDeviceQueue = nullptr;
	PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR = nullptr;
	PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR = nullptr;
	PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR = nullptr;
	PFN_vkCreateImageView vkCreateImageView = nullptr;
	PFN_vkDestroyImageView vkDestroyImageView = nullptr;
	PFN_vkCreateCommandPool vkCreateCommandPool = nullptr;
	PFN_vkDestroyCommandPool vkDestroyCommandPool = nullptr;
	PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers = nullptr;
	PFN_vkFreeCommandBuffers vkFreeCommandBuffers = nullptr;
	PFN_vkResetCommandBuffer vkResetCommandBuffer = nullptr;
	PFN_vkBeginCommandBuffer vkBeginCommandBuffer = nullptr;
	PFN_vkEndCommandBuffer vkEndCommandBuffer = nullptr;
	PFN_vkCmdClearColorImage vkCmdClearColorImage = nullptr;
	PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier = nullptr;
	PFN_vkCreateSemaphore vkCreateSemaphore = nullptr;
	PFN_vkDestroySemaphore vkDestroySemaphore = nullptr;
	PFN_vkCreateFence vkCreateFence = nullptr;
	PFN_vkDestroyFence vkDestroyFence = nullptr;
	PFN_vkWaitForFences vkWaitForFences = nullptr;
	PFN_vkResetFences vkResetFences = nullptr;
	PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR = nullptr;
	PFN_vkQueueSubmit vkQueueSubmit = nullptr;
	PFN_vkQueuePresentKHR vkQueuePresentKHR = nullptr;
	PFN_vkQueueWaitIdle vkQueueWaitIdle = nullptr;

	PFN_vkCreateBuffer vkCreateBuffer = nullptr;
	PFN_vkDestroyBuffer vkDestroyBuffer = nullptr;
	PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements = nullptr;
	PFN_vkBindBufferMemory vkBindBufferMemory = nullptr;
	PFN_vkCreateImage vkCreateImage = nullptr;
	PFN_vkDestroyImage vkDestroyImage = nullptr;
	PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements = nullptr;
	PFN_vkBindImageMemory vkBindImageMemory = nullptr;
	PFN_vkAllocateMemory vkAllocateMemory = nullptr;
	PFN_vkFreeMemory vkFreeMemory = nullptr;
	PFN_vkMapMemory vkMapMemory = nullptr;
	PFN_vkUnmapMemory vkUnmapMemory = nullptr;
	PFN_vkCreateSampler vkCreateSampler = nullptr;
	PFN_vkDestroySampler vkDestroySampler = nullptr;
	PFN_vkCreateShaderModule vkCreateShaderModule = nullptr;
	PFN_vkDestroyShaderModule vkDestroyShaderModule = nullptr;
	PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout = nullptr;
	PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout = nullptr;
	PFN_vkCreateDescriptorPool vkCreateDescriptorPool = nullptr;
	PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool = nullptr;
	PFN_vkResetDescriptorPool vkResetDescriptorPool = nullptr;
	PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets = nullptr;
	PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets = nullptr;
	PFN_vkCreatePipelineLayout vkCreatePipelineLayout = nullptr;
	PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout = nullptr;
	PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines = nullptr;
	PFN_vkDestroyPipeline vkDestroyPipeline = nullptr;
	PFN_vkCreatePipelineCache vkCreatePipelineCache = nullptr;
	PFN_vkDestroyPipelineCache vkDestroyPipelineCache = nullptr;
	PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer = nullptr;
	PFN_vkCreateRenderPass vkCreateRenderPass = nullptr;
	PFN_vkDestroyRenderPass vkDestroyRenderPass = nullptr;
	PFN_vkCreateFramebuffer vkCreateFramebuffer = nullptr;
	PFN_vkDestroyFramebuffer vkDestroyFramebuffer = nullptr;
	PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass = nullptr;
	PFN_vkCmdEndRenderPass vkCmdEndRenderPass = nullptr;
	PFN_vkCmdBindPipeline vkCmdBindPipeline = nullptr;
	PFN_vkCmdBindVertexBuffers vkCmdBindVertexBuffers = nullptr;
	PFN_vkCmdBindIndexBuffer vkCmdBindIndexBuffer = nullptr;
	PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets = nullptr;
	PFN_vkCmdSetViewport vkCmdSetViewport = nullptr;
	PFN_vkCmdSetScissor vkCmdSetScissor = nullptr;
	PFN_vkCmdDraw vkCmdDraw = nullptr;
	PFN_vkCmdDrawIndexed vkCmdDrawIndexed = nullptr;
	PFN_vkCmdCopyBuffer vkCmdCopyBuffer = nullptr;
	PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage = nullptr;
	PFN_vkCmdClearAttachments vkCmdClearAttachments = nullptr;
	PFN_vkCmdBlitImage vkCmdBlitImage = nullptr;

	namespace
	{
		// -- Owned Vulkan objects --
		VkInstance s_instance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT s_debugMessenger = VK_NULL_HANDLE;
		VkPhysicalDevice s_physicalDevice = VK_NULL_HANDLE;
		VkPhysicalDeviceMemoryProperties s_memProps = {};
		VkDevice s_device = VK_NULL_HANDLE;
		VkSurfaceKHR s_surface = VK_NULL_HANDLE;
		VkQueue s_graphicsQueue = VK_NULL_HANDLE;
		VkQueue s_presentQueue = VK_NULL_HANDLE;
		std::uint32_t s_graphicsFamily = 0;
		std::uint32_t s_presentFamily = 0;
		VkDeviceSize s_minUboAlign = 256;
		bool s_depthClamp = false;

		VkSwapchainKHR s_swapchain = VK_NULL_HANDLE;
		VkFormat s_swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
		VkExtent2D s_swapchainExtent = { 0, 0 };
		std::vector<VkImage> s_swapchainImages;
		std::vector<VkImageView> s_swapchainImageViews;

		VkCommandPool s_commandPool = VK_NULL_HANDLE;

		// -- Frames in flight --
		// Each in-flight frame owns a command buffer, an image-available semaphore, an in-flight fence, its own
		// descriptor pools and a slice of the uniform ring, so the CPU can record frame N+1 while the GPU still
		// runs frame N (BeginFrame waits only THIS slot's fence, not a global idle). The render-finished semaphore
		// is per-SWAP-CHAIN-IMAGE (s_renderFinishedPerImage), never per-frame, so a binary semaphore is never
		// re-signalled before the present that waits on it has consumed it - the resize / out-of-date hazard the
		// old single-frame, two-binary-semaphore model deadlocked on.
		// One frame in flight (serial): BeginFrame waits this slot's fence, so the previous frame's GPU work is
		// fully complete before the next frame records/executes. This is REQUIRED for correctness here because the
		// engine renders every frame into SHARED, single-instance render targets (the scene/bloom/lighting/combine
		// targets, the screen image, and the per-frame-updated palette texture) that are reused across frames. With
		// >1 frame in flight, frame N+1 would start writing those shared targets while the GPU still reads them for
		// frame N - a cross-frame write-after-read the engine's intra-frame render-pass (VK_SUBPASS_EXTERNAL)
		// dependencies do NOT cover - which surfaces as rare, random one-frame flashes of wrong content. Serial
		// execution removes that race by construction. (Raising this again would require double-buffering every
		// engine render target per frame slot; the machinery below stays parameterized on this constant so that is
		// possible later.) The other Vulkan perf wins - no per-frame palette upload stall, within-frame descriptor/
		// UBO reuse, per-swap-chain-image render-finished semaphores, deferred resource destruction - are all
		// independent of the in-flight count and remain in effect.
		static constexpr std::uint32_t MaxFramesInFlight = 1;
		struct FrameData
		{
			VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
			VkSemaphore ImageAvailable = VK_NULL_HANDLE;
			VkFence InFlightFence = VK_NULL_HANDLE;
			bool FenceInFlight = false;							// true only between a successful submit and the next BeginFrame wait
			std::vector<VkDescriptorPool> DescPools;			// per-frame; reset when this slot is re-entered
			std::size_t DescPoolCursor = 0;
			VkDeviceSize UboCursor = 0;							// relative to this frame's uniform-ring region base
			std::vector<VkBuffer> PendingStagingBuffers;		// texture-upload staging, freed once this slot's fence re-signals
			std::vector<VkDeviceMemory> PendingStagingMemory;
		};
		FrameData s_frames[MaxFramesInFlight];
		std::uint32_t s_currentFrame = 0;
		VkCommandBuffer s_commandBuffer = VK_NULL_HANDLE;	// == s_frames[s_currentFrame].CommandBuffer while recording (set in BeginFrame)

		// One render-finished semaphore per swap-chain image (signalled by submit, waited by present) plus a fence
		// per image tracking which in-flight frame last used it, so an image is never re-acquired while a previous
		// frame that targeted it is still running (the standard imagesInFlight pattern).
		std::vector<VkSemaphore> s_renderFinishedPerImage;
		std::vector<VkFence> s_imagesInFlight;

		// The "screen" (default framebuffer): every no-render-target draw goes here, then PresentFrame() blits
		// it (vertically flipped) into the acquired swap-chain image. The whole scene is rendered GL-bottom-up
		// (positive-height viewport, no projection flip), so off-screen render targets round-trip exactly like
		// GL; the single flip at present is the only GL->Vulkan scan-out correction (mirrors the software
		// backend's SDL_FLIP_VERTICAL and the D3D11 flip-blit).
		VkImage s_screenImage = VK_NULL_HANDLE;
		VkDeviceMemory s_screenMemory = VK_NULL_HANDLE;
		VkImageView s_screenView = VK_NULL_HANDLE;
		VkFramebuffer s_screenFramebuffer = VK_NULL_HANDLE;
		VkImageLayout s_screenLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkExtent2D s_screenExtent = { 0, 0 };

		bool s_vsync = true;
		bool s_ready = false;
		void* s_sdlWindow = nullptr;

		// Per-frame uniform ring (host-visible, persistently mapped): every draw's _Globals + uniform-block
		// bytes are copied here at device-aligned offsets, so the descriptor's UBO bind offset always meets
		// minUniformBufferOffsetAlignment (the streaming buffer's own alignment can be smaller).
		VkBuffer s_uboRing = VK_NULL_HANDLE;
		VkDeviceMemory s_uboRingMem = VK_NULL_HANDLE;
		std::uint8_t* s_uboRingMapped = nullptr;
		VkDeviceSize s_uboRingSize = 0;
		VkDeviceSize s_uboPerFrameSize = 0;	// s_uboRingSize / MaxFramesInFlight; frame i uses [i*this, (i+1)*this)

		// Descriptor pools are owned per in-flight frame (see FrameData::DescPools); a set is allocated per draw
		// from the current frame's pools and the whole frame's pools are reset when that slot is re-entered.

		// Within-frame reuse caches (cleared each BeginFrame, current-frame only): draws with identical uniform
		// CONTENTS share one uniform-ring region, and draws with an identical binding tuple (program + textures +
		// uniform region) share one descriptor set - so the common run of same-material draws skips the per-draw
		// vkAllocateDescriptorSets + vkUpdateDescriptorSets + ring copy. Keyed by 64-bit FNV-1a (as s_pipelines is);
		// UBO reuse additionally memcmp-verifies the bytes on a hash hit, so it is collision-proof.
		std::unordered_map<std::uint64_t, VkDeviceSize> s_frameUboByHash;
		std::unordered_map<std::uint64_t, VkDescriptorSet> s_frameSetByHash;
		std::vector<std::uint8_t> s_uboScratch;	// reused uniform-gather buffer

		// Single-color-attachment render passes keyed by format (loadOp LOAD, layout COLOR_ATTACHMENT_OPTIMAL)
		std::unordered_map<std::uint32_t, VkRenderPass> s_renderPasses;

		// 1x1 white fallback texture bound when a sampler has no texture (keeps the descriptor valid)
		VkImage s_dummyImage = VK_NULL_HANDLE;
		VkDeviceMemory s_dummyMemory = VK_NULL_HANDLE;
		VkImageView s_dummyView = VK_NULL_HANDLE;
		VkSampler s_dummySampler = VK_NULL_HANDLE;

		// -- Graphics pipeline cache --
		struct PipelineKey
		{
			std::uint32_t programHandle;
			std::uint64_t renderPass;
			std::uint32_t stride;
			std::uint32_t blendKey;
			std::uint32_t rasterKey;
			std::uint32_t topology;
			std::uint32_t hasVertexInput;
			bool operator==(const PipelineKey& o) const {
				return programHandle == o.programHandle && renderPass == o.renderPass && stride == o.stride &&
					blendKey == o.blendKey && rasterKey == o.rasterKey && topology == o.topology && hasVertexInput == o.hasVertexInput;
			}
		};
		struct PipelineKeyHash
		{
			std::size_t operator()(const PipelineKey& k) const {
				std::uint64_t h = 1469598103934665603ull;
				auto mix = [&h](std::uint64_t v) { h ^= v; h *= 1099511628211ull; };
				mix(k.programHandle); mix(k.renderPass); mix(k.stride);
				mix(k.blendKey); mix(k.rasterKey); mix(std::uint64_t(k.topology) | (std::uint64_t(k.hasVertexInput) << 32));
				return std::size_t(h);
			}
		};
		std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> s_pipelines;

		// -- Frame recording state --
		bool s_frameActive = false;
		bool s_renderPassOpen = false;
		VulkanRenderTarget* s_activeRenderPassTarget = nullptr;

		bool CheckVk(VkResult result, const char* what)
		{
			if (result != VK_SUCCESS) {
				LOGE("{} failed (VkResult {})", what, static_cast<std::int32_t>(result));
				return false;
			}
			return true;
		}

		// Defined near the loader at the bottom; forward-declared so CreateSwapchain can reference it
		VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
			VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user);

		VkPrimitiveTopology MapPrimitive(PrimitiveType p)
		{
			switch (p) {
				case PrimitiveType::Points: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
				case PrimitiveType::Lines: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
				case PrimitiveType::LineStrip:
				case PrimitiveType::LineLoop: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
				case PrimitiveType::Triangles: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
				case PrimitiveType::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
				case PrimitiveType::TriangleFan: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
				default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			}
		}

		std::uint32_t BlendCode(nCine::BlendingFactor f)
		{
			switch (f) {
				case nCine::BlendingFactor::Zero: return 0;
				case nCine::BlendingFactor::One: return 1;
				case nCine::BlendingFactor::SrcColor: return 2;
				case nCine::BlendingFactor::OneMinusSrcColor: return 3;
				case nCine::BlendingFactor::SrcAlpha: return 4;
				case nCine::BlendingFactor::OneMinusSrcAlpha: return 5;
				case nCine::BlendingFactor::DstAlpha: return 6;
				case nCine::BlendingFactor::OneMinusDstAlpha: return 7;
				case nCine::BlendingFactor::DstColor: return 8;
				case nCine::BlendingFactor::OneMinusDstColor: return 9;
				case nCine::BlendingFactor::SrcAlphaSaturate: return 10;
				case nCine::BlendingFactor::ConstantColor: return 11;
				case nCine::BlendingFactor::OneMinusConstantColor: return 12;
				case nCine::BlendingFactor::ConstantAlpha: return 13;
				case nCine::BlendingFactor::OneMinusConstantAlpha: return 14;
				default: return 1;
			}
		}

		VkBlendFactor MapBlend(nCine::BlendingFactor f)
		{
			// Vulkan applies each factor per-component (the alpha channel of e.g. SRC_COLOR is the source
			// alpha), so unlike the D3D11 backend no separate color/alpha remapping is needed.
			switch (f) {
				case nCine::BlendingFactor::Zero: return VK_BLEND_FACTOR_ZERO;
				case nCine::BlendingFactor::One: return VK_BLEND_FACTOR_ONE;
				case nCine::BlendingFactor::SrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
				case nCine::BlendingFactor::OneMinusSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
				case nCine::BlendingFactor::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
				case nCine::BlendingFactor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				case nCine::BlendingFactor::DstAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
				case nCine::BlendingFactor::OneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
				case nCine::BlendingFactor::DstColor: return VK_BLEND_FACTOR_DST_COLOR;
				case nCine::BlendingFactor::OneMinusDstColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
				case nCine::BlendingFactor::SrcAlphaSaturate: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
				case nCine::BlendingFactor::ConstantColor: return VK_BLEND_FACTOR_CONSTANT_COLOR;
				case nCine::BlendingFactor::OneMinusConstantColor: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
				case nCine::BlendingFactor::ConstantAlpha: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
				case nCine::BlendingFactor::OneMinusConstantAlpha: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
				default: return VK_BLEND_FACTOR_ONE;
			}
		}

		VkFormat AttributeFormat(std::int32_t componentCount)
		{
			switch (componentCount) {
				case 1: return VK_FORMAT_R32_SFLOAT;
				case 2: return VK_FORMAT_R32G32_SFLOAT;
				case 3: return VK_FORMAT_R32G32B32_SFLOAT;
				default: return VK_FORMAT_R32G32B32A32_SFLOAT;
			}
		}

		void ImageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
			VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, VkAccessFlags srcAccess, VkAccessFlags dstAccess)
		{
			VkImageMemoryBarrier b = {};
			b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			b.oldLayout = oldLayout;
			b.newLayout = newLayout;
			b.srcAccessMask = srcAccess;
			b.dstAccessMask = dstAccess;
			b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			b.image = image;
			b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
		}

		VkDescriptorPool CreateDescriptorPool()
		{
			VkDescriptorPoolSize sizes[2];
			sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			sizes[0].descriptorCount = 4096;
			sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			sizes[1].descriptorCount = 4096;
			VkDescriptorPoolCreateInfo pci = {};
			pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			pci.maxSets = 4096;
			pci.poolSizeCount = 2;
			pci.pPoolSizes = sizes;
			VkDescriptorPool pool = VK_NULL_HANDLE;
			vkCreateDescriptorPool(s_device, &pci, nullptr, &pool);
			return pool;
		}

		VkDescriptorSet AllocDescriptorSet(VkDescriptorSetLayout layout)
		{
			FrameData& fr = s_frames[s_currentFrame];
			for (;;) {
				if (fr.DescPoolCursor >= fr.DescPools.size()) {
					VkDescriptorPool pool = CreateDescriptorPool();
					if (pool == VK_NULL_HANDLE) {
						return VK_NULL_HANDLE;
					}
					fr.DescPools.push_back(pool);
				}
				VkDescriptorSetAllocateInfo ai = {};
				ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				ai.descriptorPool = fr.DescPools[fr.DescPoolCursor];
				ai.descriptorSetCount = 1;
				ai.pSetLayouts = &layout;
				VkDescriptorSet set = VK_NULL_HANDLE;
				const VkResult r = vkAllocateDescriptorSets(s_device, &ai, &set);
				if (r == VK_SUCCESS) {
					return set;
				}
				// Pool exhausted/fragmented: advance to the next pool (creating one if needed) and retry
				fr.DescPoolCursor++;
			}
		}

		// Bump-allocates an aligned range within the CURRENT frame's slice of the uniform ring; returns the
		// absolute byte offset into s_uboRing. Each in-flight frame owns a disjoint [base, base+perFrame) region
		// so a host write for frame N+1 never lands in bytes the GPU still reads for frame N.
		bool AllocUbo(VkDeviceSize size, VkDeviceSize& outOffset)
		{
			if (size == 0) {
				size = 16;
			}
			FrameData& fr = s_frames[s_currentFrame];
			const VkDeviceSize base = VkDeviceSize(s_currentFrame) * s_uboPerFrameSize;
			const VkDeviceSize alignedRel = (fr.UboCursor + s_minUboAlign - 1) & ~(s_minUboAlign - 1);
			if (alignedRel + size > s_uboPerFrameSize) {
				return false;
			}
			outOffset = base + alignedRel;
			fr.UboCursor = alignedRel + size;
			return true;
		}

		// -- Deferred resource destruction (see VkEnqueueDestroy in VulkanCommon.h) --
		// A resource enqueued for destruction may still be referenced by any of the frames that were in flight at
		// enqueue time (up to MaxFramesInFlight). Each entry carries a bitmask of the frame slots whose in-flight
		// frame must still complete before it is safe to free; the mask starts with every slot's bit set. In
		// BeginFrame, after a slot's fence has signalled (that slot's previous frame is done), that slot's bit is
		// cleared on every pending entry; once an entry's mask reaches zero, every frame that could have referenced
		// it has completed and it is freed. (A bitmask rather than a countdown so a slot whose fence is waited twice
		// - e.g. an out-of-date-drop path that does not advance the frame index - can never free an entry early.)
		struct PendingDestroy
		{
			VkDeferredResource Type;
			std::uint64_t Handle;
			std::uint32_t PendingSlots;
		};
		std::vector<PendingDestroy> s_pendingDestroys;

		void FreeDeferredResource(const PendingDestroy& p)
		{
			if (s_device == VK_NULL_HANDLE || p.Handle == 0) {
				return;
			}
			switch (p.Type) {
				case VkDeferredResource::Image: vkDestroyImage(s_device, reinterpret_cast<VkImage>(p.Handle), nullptr); break;
				case VkDeferredResource::ImageView: vkDestroyImageView(s_device, reinterpret_cast<VkImageView>(p.Handle), nullptr); break;
				case VkDeferredResource::Sampler: vkDestroySampler(s_device, reinterpret_cast<VkSampler>(p.Handle), nullptr); break;
				case VkDeferredResource::Buffer: vkDestroyBuffer(s_device, reinterpret_cast<VkBuffer>(p.Handle), nullptr); break;
				case VkDeferredResource::DeviceMemory: vkFreeMemory(s_device, reinterpret_cast<VkDeviceMemory>(p.Handle), nullptr); break;
				case VkDeferredResource::Framebuffer: vkDestroyFramebuffer(s_device, reinterpret_cast<VkFramebuffer>(p.Handle), nullptr); break;
			}
		}

		// Clears the just-signalled slot's bit on every pending entry and frees those no in-flight frame can still
		// reference. Called from BeginFrame right after waiting s_currentFrame's fence (so that slot's prior frame -
		// the only one that could have owned that bit - is guaranteed complete before the bit is cleared).
		void TickDeferredDestroys(std::uint32_t signalledSlot)
		{
			if (s_pendingDestroys.empty()) {
				return;
			}
			const std::uint32_t slotBit = (1u << signalledSlot);
			for (std::size_t i = 0; i < s_pendingDestroys.size(); ) {
				PendingDestroy& p = s_pendingDestroys[i];
				p.PendingSlots &= ~slotBit;
				if (p.PendingSlots == 0) {
					FreeDeferredResource(p);
					s_pendingDestroys[i] = s_pendingDestroys.back();
					s_pendingDestroys.pop_back();
				} else {
					i++;
				}
			}
		}

		// Frees every pending resource immediately, ignoring the fence bitmask. ONLY safe after a vkDeviceWaitIdle
		// (no frame is in flight) - used by the resize / device-teardown paths so nothing leaks or dangles.
		void FlushDeferredDestroys()
		{
			for (const PendingDestroy& p : s_pendingDestroys) {
				FreeDeferredResource(p);
			}
			s_pendingDestroys.clear();
		}
	}

	// -- Static state --

	VulkanDevice::BlendingState VulkanDevice::blending_;
	VulkanDevice::DepthTestState VulkanDevice::depthTest_;
	VulkanDevice::CullFaceState VulkanDevice::cullFace_;
	VulkanDevice::ScissorState VulkanDevice::scissor_;
	Recti VulkanDevice::viewport_(0, 0, 0, 0);
	Colorf VulkanDevice::clearColor_(0.0f, 0.0f, 0.0f, 0.0f);

	VulkanShaderProgram* VulkanDevice::currentProgram_ = nullptr;
	const VulkanTexture* VulkanDevice::boundTextures_[MaxTextureUnits] = {};
	VulkanDevice::UniformRange VulkanDevice::boundUniformRanges_[MaxUniformBindings] = {};
	VulkanRenderTarget* VulkanDevice::currentRenderTarget_ = nullptr;

	// -- Shared context accessors (declared in VulkanCommon.h) --

	VkDevice VkDeviceHandle() { return s_device; }
	VkPhysicalDevice VkPhysicalDeviceHandle() { return s_physicalDevice; }
	VkDeviceSize VkMinUniformBufferOffsetAlignment() { return s_minUboAlign; }

	std::uint32_t VkFindMemoryType(std::uint32_t typeBits, VkMemoryPropertyFlags properties)
	{
		for (std::uint32_t i = 0; i < s_memProps.memoryTypeCount; i++) {
			if ((typeBits & (1u << i)) != 0 && (s_memProps.memoryTypes[i].propertyFlags & properties) == properties) {
				return i;
			}
		}
		return UINT32_MAX;
	}

	VkCommandBuffer VkBeginOneTimeCommands()
	{
		if (s_device == VK_NULL_HANDLE || s_commandPool == VK_NULL_HANDLE) {
			return VK_NULL_HANDLE;
		}
		VkCommandBufferAllocateInfo ai = {};
		ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		ai.commandPool = s_commandPool;
		ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		ai.commandBufferCount = 1;
		VkCommandBuffer cmd = VK_NULL_HANDLE;
		if (vkAllocateCommandBuffers(s_device, &ai, &cmd) != VK_SUCCESS) {
			return VK_NULL_HANDLE;
		}
		VkCommandBufferBeginInfo bi = {};
		bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(cmd, &bi);
		return cmd;
	}

	void VkEndOneTimeCommands(VkCommandBuffer cmd)
	{
		if (cmd == VK_NULL_HANDLE) {
			return;
		}
		vkEndCommandBuffer(cmd);
		VkSubmitInfo si = {};
		si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		si.commandBufferCount = 1;
		si.pCommandBuffers = &cmd;
		vkQueueSubmit(s_graphicsQueue, 1, &si, VK_NULL_HANDLE);
		vkQueueWaitIdle(s_graphicsQueue);
		vkFreeCommandBuffers(s_device, s_commandPool, 1, &cmd);
	}

	void VkRegisterFrameStaging(std::uint64_t buffer, std::uint64_t memory)
	{
		// The staging buffer for a texture upload recorded into the current frame's command buffer must outlive
		// the GPU's copy. Hand it to the current in-flight frame; it is destroyed the next time that slot is
		// entered (after its fence has signalled - see FreeFrameStaging in BeginFrame), never with a queue idle.
		FrameData& fr = s_frames[s_currentFrame];
		if (buffer != 0) {
			fr.PendingStagingBuffers.push_back(reinterpret_cast<VkBuffer>(buffer));
		}
		if (memory != 0) {
			fr.PendingStagingMemory.push_back(reinterpret_cast<VkDeviceMemory>(memory));
		}
	}

	void VkEnqueueDestroy(VkDeferredResource type, std::uint64_t handle)
	{
		if (handle == 0) {
			return;
		}
		if (s_device == VK_NULL_HANDLE) {
			// The device (and every child object it owns) is already gone: nothing to free, nothing in flight.
			return;
		}
		// The resource must outlive every frame currently in flight, so it is held until all MaxFramesInFlight
		// frame slots have completed one more frame (each bit cleared by TickDeferredDestroys as its fence signals).
		const std::uint32_t allSlots = (MaxFramesInFlight >= 32 ? 0xFFFFFFFFu : ((1u << MaxFramesInFlight) - 1u));
		s_pendingDestroys.push_back(PendingDestroy{ type, handle, allSlots });
	}

	namespace
	{
		// Destroys the staging buffers/memory a frame slot queued last time it was recorded (safe: the caller has
		// already waited that slot's fence, so the GPU is done reading them)
		void FreeFrameStaging(FrameData& fr)
		{
			for (VkBuffer b : fr.PendingStagingBuffers) {
				if (b != VK_NULL_HANDLE) { vkDestroyBuffer(s_device, b, nullptr); }
			}
			for (VkDeviceMemory m : fr.PendingStagingMemory) {
				if (m != VK_NULL_HANDLE) { vkFreeMemory(s_device, m, nullptr); }
			}
			fr.PendingStagingBuffers.clear();
			fr.PendingStagingMemory.clear();
		}
	}

	VkRenderPass VkGetColorRenderPass(VkFormat format)
	{
		auto it = s_renderPasses.find(std::uint32_t(format));
		if (it != s_renderPasses.end()) {
			return it->second;
		}
		VkAttachmentDescription color = {};
		color.format = format;
		color.samples = VK_SAMPLE_COUNT_1_BIT;
		color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;			// contents preserved; the engine clears explicitly
		color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		color.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference ref = {};
		ref.attachment = 0;
		ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &ref;

		VkSubpassDependency deps[2] = {};
		deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		deps[0].dstSubpass = 0;
		deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
		deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
		deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		deps[1].srcSubpass = 0;
		deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
		deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;

		VkRenderPassCreateInfo rpci = {};
		rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		rpci.attachmentCount = 1;
		rpci.pAttachments = &color;
		rpci.subpassCount = 1;
		rpci.pSubpasses = &subpass;
		rpci.dependencyCount = 2;
		rpci.pDependencies = deps;
		VkRenderPass rp = VK_NULL_HANDLE;
		if (vkCreateRenderPass(s_device, &rpci, nullptr, &rp) != VK_SUCCESS) {
			return VK_NULL_HANDLE;
		}
		s_renderPasses[std::uint32_t(format)] = rp;
		return rp;
	}

	// -- Pipeline cache --

	namespace
	{
		// Driver-level pipeline cache passed to every vkCreateGraphicsPipelines, so pipelines recreated after a
		// swapchain rebuild (s_pipelines is dropped on resize) reuse the driver's cached compilation instead of
		// recompiling the SPIR-V from scratch. Created lazily, destroyed with the device.
		VkPipelineCache s_pipelineCache = VK_NULL_HANDLE;

		// Last-bound state of the current command buffer (redundant vkCmd* elimination). Command-buffer state is
		// reset by vkResetCommandBuffer, so BeginFrame() clears these; deferred destruction only frees GPU objects
		// at that same boundary, so a recycled handle can never alias a value still cached here mid-frame.
		VkPipeline s_lastPipeline = VK_NULL_HANDLE;
		VkDescriptorSet s_lastDescriptorSet = VK_NULL_HANDLE;
		VkPipelineLayout s_lastDescriptorLayout = VK_NULL_HANDLE;
		VkBuffer s_lastVertexBuffer = VK_NULL_HANDLE;
		VkBuffer s_lastIndexBuffer = VK_NULL_HANDLE;
		VkIndexType s_lastIndexType = VK_INDEX_TYPE_MAX_ENUM;
		VkViewport s_lastViewport = {};
		VkRect2D s_lastScissor = {};
		bool s_lastViewportValid = false;
		bool s_lastScissorValid = false;

		void ResetCommandBufferShadowState()
		{
			s_lastPipeline = VK_NULL_HANDLE;
			s_lastDescriptorSet = VK_NULL_HANDLE;
			s_lastDescriptorLayout = VK_NULL_HANDLE;
			s_lastVertexBuffer = VK_NULL_HANDLE;
			s_lastIndexBuffer = VK_NULL_HANDLE;
			s_lastIndexType = VK_INDEX_TYPE_MAX_ENUM;
			s_lastViewportValid = false;
			s_lastScissorValid = false;
		}

		VkPipeline GetOrCreatePipeline(VulkanShaderProgram* prog, PrimitiveType primitive, VkRenderPass renderPass)
		{
			VulkanShaderProgram::VertexAttrib attribs[16];
			std::uint32_t attribCount = 0;
			std::uint32_t stride = 0;
			const bool hasVI = prog->HasVertexAttributes();
			if (hasVI) {
				attribCount = prog->GetVertexInput(attribs, 16, stride);
			}

			const VulkanDevice::BlendingState blend = VulkanDevice::GetBlendingState();
			const VulkanDevice::CullFaceState cull = VulkanDevice::GetCullFaceState();
			const std::uint32_t blendKey = (blend.Enabled ? 1u : 0u) | (BlendCode(blend.SrcRgb) << 1) |
				(BlendCode(blend.DstRgb) << 5) | (BlendCode(blend.SrcAlpha) << 9) | (BlendCode(blend.DstAlpha) << 13);
			const std::uint32_t rasterKey = (cull.Enabled ? 1u : 0u) | (std::uint32_t(cull.Mode) << 1);

			PipelineKey key;
			key.programHandle = prog->GetGLHandle();
			key.renderPass = reinterpret_cast<std::uint64_t>(renderPass);
			key.stride = stride;
			key.blendKey = blendKey;
			key.rasterKey = rasterKey;
			key.topology = std::uint32_t(MapPrimitive(primitive));
			key.hasVertexInput = hasVI ? 1u : 0u;

			auto it = s_pipelines.find(key);
			if (it != s_pipelines.end()) {
				return it->second;
			}

			VkPipelineShaderStageCreateInfo stages[2] = {};
			stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			stages[0].module = reinterpret_cast<VkShaderModule>(prog->GetVsModule());
			stages[0].pName = "main";
			stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[1].module = reinterpret_cast<VkShaderModule>(prog->GetFsModule());
			stages[1].pName = "main";

			VkVertexInputBindingDescription binding = {};
			VkVertexInputAttributeDescription viAttribs[16] = {};
			std::uint32_t viAttribCount = 0;
			if (hasVI && stride > 0) {
				binding.binding = 0;
				binding.stride = stride;
				binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
				for (std::uint32_t i = 0; i < attribCount; i++) {
					VkVertexInputAttributeDescription& d = viAttribs[viAttribCount++];
					d.location = attribs[i].Location;
					d.binding = 0;
					d.format = AttributeFormat(attribs[i].ComponentCount);
					d.offset = attribs[i].Offset;
				}
			}
			VkPipelineVertexInputStateCreateInfo vi = {};
			vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			if (hasVI && stride > 0 && viAttribCount > 0) {
				vi.vertexBindingDescriptionCount = 1;
				vi.pVertexBindingDescriptions = &binding;
				vi.vertexAttributeDescriptionCount = viAttribCount;
				vi.pVertexAttributeDescriptions = viAttribs;
			}

			VkPipelineInputAssemblyStateCreateInfo ia = {};
			ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			ia.topology = MapPrimitive(primitive);

			VkPipelineViewportStateCreateInfo vp = {};
			vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			vp.viewportCount = 1;
			vp.scissorCount = 1;

			VkPipelineRasterizationStateCreateInfo rs = {};
			rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rs.polygonMode = VK_POLYGON_MODE_FILL;
			rs.cullMode = cull.Enabled
				? (cull.Mode == CullFaceMode::Front ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT)
				: VK_CULL_MODE_NONE;
			rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;		// GL default winding
			rs.lineWidth = 1.0f;
			// The engine's GL ortho produces clip.z in [-1,1]; enabling depth clamp disables near/far clipping
			// so nothing is culled by Vulkan's [0,1] depth range (depth testing is off anyway - a 2D renderer).
			rs.depthClampEnable = s_depthClamp ? VK_TRUE : VK_FALSE;

			VkPipelineMultisampleStateCreateInfo ms = {};
			ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			VkPipelineDepthStencilStateCreateInfo ds = {};
			ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			ds.depthTestEnable = VK_FALSE;
			ds.depthWriteEnable = VK_FALSE;
			ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;

			VkPipelineColorBlendAttachmentState cba = {};
			cba.blendEnable = blend.Enabled ? VK_TRUE : VK_FALSE;
			cba.srcColorBlendFactor = MapBlend(blend.SrcRgb);
			cba.dstColorBlendFactor = MapBlend(blend.DstRgb);
			cba.colorBlendOp = VK_BLEND_OP_ADD;
			cba.srcAlphaBlendFactor = MapBlend(blend.SrcAlpha);
			cba.dstAlphaBlendFactor = MapBlend(blend.DstAlpha);
			cba.alphaBlendOp = VK_BLEND_OP_ADD;
			cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			VkPipelineColorBlendStateCreateInfo cb = {};
			cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			cb.attachmentCount = 1;
			cb.pAttachments = &cba;

			const VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
			VkPipelineDynamicStateCreateInfo dyn = {};
			dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dyn.dynamicStateCount = 2;
			dyn.pDynamicStates = dynStates;

			VkGraphicsPipelineCreateInfo gpci = {};
			gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			gpci.stageCount = 2;
			gpci.pStages = stages;
			gpci.pVertexInputState = &vi;
			gpci.pInputAssemblyState = &ia;
			gpci.pViewportState = &vp;
			gpci.pRasterizationState = &rs;
			gpci.pMultisampleState = &ms;
			gpci.pDepthStencilState = &ds;
			gpci.pColorBlendState = &cb;
			gpci.pDynamicState = &dyn;
			gpci.layout = reinterpret_cast<VkPipelineLayout>(prog->GetPipelineLayout());
			gpci.renderPass = renderPass;
			gpci.subpass = 0;

			if (s_pipelineCache == VK_NULL_HANDLE && vkCreatePipelineCache != nullptr) {
				VkPipelineCacheCreateInfo pcci = {};
				pcci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
				vkCreatePipelineCache(s_device, &pcci, nullptr, &s_pipelineCache);
			}

			VkPipeline pipeline = VK_NULL_HANDLE;
			if (vkCreateGraphicsPipelines(s_device, s_pipelineCache, 1, &gpci, nullptr, &pipeline) != VK_SUCCESS) {
				LOGE("vkCreateGraphicsPipelines failed for program {}", prog->GetGLHandle());
				return VK_NULL_HANDLE;
			}
			s_pipelines[key] = pipeline;
			return pipeline;
		}

		// Ends the open render pass, if any
		void EndRenderPassIfOpen()
		{
			if (s_renderPassOpen) {
				vkCmdEndRenderPass(s_commandBuffer);
				s_renderPassOpen = false;
			}
		}

		// Transitions an image to COLOR_ATTACHMENT_OPTIMAL from its tracked layout (outside a render pass)
		void ToColorAttachment(VkImage image, VkImageLayout& layout)
		{
			if (layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
				return;
			}
			ImageBarrier(s_commandBuffer, image, layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);
			layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		// Ensures a render pass is open for the current RHI render target (nullptr == the screen image)
		bool EnsureRenderPass(VulkanRenderTarget* target)
		{
			if (s_renderPassOpen && s_activeRenderPassTarget == target) {
				return true;
			}
			EndRenderPassIfOpen();

			VkFramebuffer framebuffer = VK_NULL_HANDLE;
			VkRenderPass renderPass = VK_NULL_HANDLE;
			VkExtent2D extent = { 0, 0 };
			if (target == nullptr) {
				if (s_screenFramebuffer == VK_NULL_HANDLE) {
					return false;
				}
				ToColorAttachment(s_screenImage, s_screenLayout);
				framebuffer = s_screenFramebuffer;
				renderPass = VkGetColorRenderPass(s_swapchainFormat);
				extent = s_screenExtent;
			} else {
				VulkanTexture* color = target->GetColorTexture(0);
				const std::uint64_t fb = target->GetFramebuffer();
				if (color == nullptr || fb == 0) {
					return false;
				}
				VkImage image = reinterpret_cast<VkImage>(color->GpuImage());
				VkImageLayout layout = VkImageLayout(color->GetCurrentLayout());
				ToColorAttachment(image, layout);
				color->SetCurrentLayout(std::uint32_t(layout));
				framebuffer = reinterpret_cast<VkFramebuffer>(fb);
				renderPass = VkGetColorRenderPass(VkFormat(color->GpuFormat()));
				extent = { std::uint32_t(color->GetWidth()), std::uint32_t(color->GetHeight()) };
			}
			if (framebuffer == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE || extent.width == 0) {
				return false;
			}

			VkRenderPassBeginInfo rpbi = {};
			rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			rpbi.renderPass = renderPass;
			rpbi.framebuffer = framebuffer;
			rpbi.renderArea.offset = { 0, 0 };
			rpbi.renderArea.extent = extent;
			vkCmdBeginRenderPass(s_commandBuffer, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
			s_renderPassOpen = true;
			s_activeRenderPassTarget = target;
			return true;
		}
	}

	// -- Frame lifecycle --

	namespace
	{
		void BeginFrame()
		{
			if (s_frameActive || !s_ready || s_device == VK_NULL_HANDLE) {
				return;
			}
			FrameData& fr = s_frames[s_currentFrame];
			// Wait for THIS frame slot's previous GPU work (NOT a global idle): its command buffer, descriptor
			// pools and uniform-ring region then become safe to reuse. With MaxFramesInFlight slots the CPU can
			// record this frame while the GPU still runs the other slot's frame. The fence is only waited on when
			// a submit actually put it in flight (FenceInFlight), so a frame that bailed before submitting - or
			// whose vkQueueSubmit failed and left the fence unsignalled - cannot deadlock this wait.
			if (fr.FenceInFlight) {
				vkWaitForFences(s_device, 1, &fr.InFlightFence, VK_TRUE, UINT64_MAX);
				fr.FenceInFlight = false;
			}

			// The fence signalled => the GPU finished the frame that queued this slot's staging buffers; free them
			FreeFrameStaging(fr);
			// ...and clear this slot's bit on the deferred-destroy queue, freeing resources no in-flight frame can
			// still reference (a resource enqueued while this slot's frame was in flight is safe once that fence signals)
			TickDeferredDestroys(s_currentFrame);

			fr.UboCursor = 0;
			for (VkDescriptorPool pool : fr.DescPools) {
				vkResetDescriptorPool(s_device, pool, 0);
			}
			fr.DescPoolCursor = 0;
			// The within-frame reuse caches reference this frame's (now reset) pools + ring region, so clear them
			s_frameUboByHash.clear();
			s_frameSetByHash.clear();

			s_commandBuffer = fr.CommandBuffer;
			vkResetCommandBuffer(s_commandBuffer, 0);
			VkCommandBufferBeginInfo bi = {};
			bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			vkBeginCommandBuffer(s_commandBuffer, &bi);
			s_frameActive = true;
			s_renderPassOpen = false;
			s_activeRenderPassTarget = nullptr;
			ResetCommandBufferShadowState();	// the reset command buffer holds no bindings any more

			// Clear the screen image to opaque black so unwritten regions are defined (the default framebuffer
			// starts cleared, like GL); the engine overwrites/clears again through its own passes. The screen
			// image is shared across in-flight frames, so the clear's source scope spans COLOR_ATTACHMENT_OUTPUT |
			// TRANSFER: via queue submission order this makes the clear wait for the OTHER in-flight frame's blit
			// read (TRANSFER) and colour writes of the screen before overwriting it (the shared-image WAR guard).
			if (s_screenImage != VK_NULL_HANDLE) {
				ImageBarrier(s_commandBuffer, s_screenImage, s_screenLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
					VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
				VkClearColorValue black = {};
				VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
				vkCmdClearColorImage(s_commandBuffer, s_screenImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
				ImageBarrier(s_commandBuffer, s_screenImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
				s_screenLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			}
		}
	}

	// -- Pipeline state (recorded; applied at draw time) --

	void VulkanDevice::SetBlendingEnabled(bool enabled) { blending_.Enabled = enabled; }

	void VulkanDevice::SetBlendingFactors(nCine::BlendingFactor srcRgb, nCine::BlendingFactor dstRgb, nCine::BlendingFactor srcAlpha, nCine::BlendingFactor dstAlpha)
	{
		blending_.SrcRgb = srcRgb;
		blending_.DstRgb = dstRgb;
		blending_.SrcAlpha = srcAlpha;
		blending_.DstAlpha = dstAlpha;
	}

	VulkanDevice::BlendingState VulkanDevice::GetBlendingState() { return blending_; }
	void VulkanDevice::SetBlendingState(const BlendingState& state) { blending_ = state; }

	void VulkanDevice::SetDepthTestEnabled(bool enabled) { depthTest_.TestEnabled = enabled; }
	void VulkanDevice::SetDepthMaskEnabled(bool enabled) { depthTest_.MaskEnabled = enabled; }
	VulkanDevice::DepthTestState VulkanDevice::GetDepthTestState() { return depthTest_; }
	void VulkanDevice::SetDepthTestState(const DepthTestState& state) { depthTest_ = state; }

	void VulkanDevice::SetCullFaceEnabled(bool enabled) { cullFace_.Enabled = enabled; }
	VulkanDevice::CullFaceState VulkanDevice::GetCullFaceState() { return cullFace_; }
	void VulkanDevice::SetCullFaceState(const CullFaceState& state) { cullFace_ = state; }

	VulkanDevice::ScissorState VulkanDevice::GetScissorState() { return scissor_; }
	void VulkanDevice::SetScissorState(const ScissorState& state) { scissor_ = state; }
	void VulkanDevice::SetScissor(const Recti& rect) { scissor_.Enabled = true; scissor_.Rect = rect; }
	void VulkanDevice::SetScissorTestEnabled(bool enabled) { scissor_.Enabled = enabled; }

	Recti VulkanDevice::GetViewport() { return viewport_; }
	void VulkanDevice::SetViewport(const Recti& rect) { viewport_ = rect; }
	void VulkanDevice::InitViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) { viewport_ = Recti(x, y, width, height); }

	Colorf VulkanDevice::GetClearColor() { return clearColor_; }
	void VulkanDevice::SetClearColor(const Colorf& color) { clearColor_ = color; }

	void VulkanDevice::Clear(ClearFlags flags)
	{
		if ((flags & ClearFlags::Color) == ClearFlags::None) {
			return;	// 2D renderer: no depth/stencil attachments
		}
		if (!s_ready) {
			return;
		}
		if (!s_frameActive) {
			BeginFrame();
		}
		if (!EnsureRenderPass(currentRenderTarget_)) {
			return;
		}
		VkClearAttachment ca = {};
		ca.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		ca.colorAttachment = 0;
		ca.clearValue.color.float32[0] = clearColor_.R;
		ca.clearValue.color.float32[1] = clearColor_.G;
		ca.clearValue.color.float32[2] = clearColor_.B;
		ca.clearValue.color.float32[3] = clearColor_.A;
		VkExtent2D extent = (currentRenderTarget_ == nullptr) ? s_screenExtent
			: VkExtent2D{ std::uint32_t(currentRenderTarget_->GetColorTexture(0) ? currentRenderTarget_->GetColorTexture(0)->GetWidth() : 0),
						  std::uint32_t(currentRenderTarget_->GetColorTexture(0) ? currentRenderTarget_->GetColorTexture(0)->GetHeight() : 0) };
		VkClearRect rect = {};
		rect.rect.offset = { 0, 0 };
		rect.rect.extent = extent;
		rect.baseArrayLayer = 0;
		rect.layerCount = 1;
		vkCmdClearAttachments(s_commandBuffer, 1, &ca, 1, &rect);
	}

	// -- Draws --

	namespace
	{
		// FNV-1a 64-bit accumulation (same basis/prime as the pipeline cache) for the within-frame reuse caches
		inline void HashBytes(std::uint64_t& h, const void* data, std::size_t len)
		{
			const std::uint8_t* p = static_cast<const std::uint8_t*>(data);
			for (std::size_t i = 0; i < len; i++) {
				h ^= p[i];
				h *= 1099511628211ull;
			}
		}
		inline void HashU64(std::uint64_t& h, std::uint64_t v)
		{
			HashBytes(h, &v, sizeof(v));
		}

		void BuildAndBindDescriptorSet(VulkanShaderProgram* prog)
		{
			const std::vector<VulkanShaderProgram::DescriptorBinding>& bindings = prog->GetDescriptorBindings();
			if (bindings.empty()) {
				return;
			}
			VkPipelineLayout layout = reinterpret_cast<VkPipelineLayout>(prog->GetPipelineLayout());

			// Small fixed-capacity scratch (a program has only a handful of bindings)
			VkDescriptorBufferInfo bufferInfos[16];
			VkDescriptorImageInfo imageInfos[16];
			VkWriteDescriptorSet writes[16];
			std::uint32_t writeCount = 0;

			// Resolve every binding's content into the writes/infos arrays and accumulate a binding-tuple hash.
			// UBO bytes are deduped by content so identical draws land on the same ring region (and thus the same
			// key), letting the whole descriptor set be reused below.
			std::uint64_t keyHash = 1469598103934665603ull;
			HashU64(keyHash, prog->GetGLHandle());

			for (const VulkanShaderProgram::DescriptorBinding& b : bindings) {
				if (writeCount >= 16) {
					static bool warnedBindingOverflow = false;
					if (!warnedBindingOverflow) {
						warnedBindingOverflow = true;
						LOGW("Program {} declares more than 16 descriptor bindings; the rest are dropped", prog->GetGLHandle());
					}
					break;
				}
				VkWriteDescriptorSet& w = writes[writeCount];
				w = {};
				w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				w.dstSet = VK_NULL_HANDLE;	// filled in below only if we actually allocate a fresh set
				w.dstBinding = b.binding;
				w.dstArrayElement = 0;
				w.descriptorCount = 1;

				if (b.kind == VulkanShaderProgram::DescriptorBinding::Kind::Sampler) {
					const VulkanTexture* tex = VulkanDevice::GetBoundTexture(std::uint32_t(b.slot >= 0 ? b.slot : 0));
					VkDescriptorImageInfo& ii = imageInfos[writeCount];
					ii = {};
					if (tex != nullptr && tex->GpuView() != 0) {
						ii.imageView = reinterpret_cast<VkImageView>(tex->GpuView());
						ii.sampler = reinterpret_cast<VkSampler>(tex->GpuSampler());
					} else {
						ii.imageView = s_dummyView;
						ii.sampler = s_dummySampler;
					}
					ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					w.pImageInfo = &ii;
					HashU64(keyHash, 0x5a3d0000u | b.binding);
					HashU64(keyHash, reinterpret_cast<std::uint64_t>(ii.imageView));
					HashU64(keyHash, reinterpret_cast<std::uint64_t>(ii.sampler));
					writeCount++;
					continue;
				}

				// UBO (Globals or a std140 block): gather the bytes into scratch, then dedupe by content
				VkDeviceSize size = 0;
				const std::uint8_t* src = nullptr;
				if (b.kind == VulkanShaderProgram::DescriptorBinding::Kind::Globals) {
					size = prog->GetGlobalsSize();
				} else {
					const std::uint32_t slot = std::uint32_t(b.slot);
					if (slot < VulkanDevice::MaxUniformBindingsPublic()) {
						const std::uint8_t* rangeData = nullptr;
						std::uint32_t rangeSize = 0;
						VulkanDevice::GetUniformRange(slot, rangeData, rangeSize);
						src = rangeData;
						size = rangeSize;
					}
				}
				if (size == 0) {
					size = 16;	// keep a valid (if unused) binding
				}
				if (s_uboScratch.size() < std::size_t(size)) {
					s_uboScratch.resize(std::size_t(size));
				}
				std::uint8_t* gather = s_uboScratch.data();
				std::memset(gather, 0, std::size_t(size));
				if (b.kind == VulkanShaderProgram::DescriptorBinding::Kind::Globals) {
					// Gather the loose uniforms into their std140 slots from the program's committed values
					for (const VulkanShaderProgram::LooseUniform& lu : prog->GetLooseUniforms()) {
						const std::uint8_t* value = prog->ResolveUniform(lu.Name.data());
						if (value != nullptr && lu.Offset + lu.Size <= size) {
							std::memcpy(gather + lu.Offset, value, lu.Size);
						}
					}
				} else if (src != nullptr) {
					std::memcpy(gather, src, std::size_t(size));
				}

				// Dedupe the ring region by content (hash then memcmp-verify => collision-proof)
				std::uint64_t contentHash = 1469598103934665603ull;
				HashU64(contentHash, std::uint64_t(size));
				HashBytes(contentHash, gather, std::size_t(size));
				VkDeviceSize offset = 0;
				auto it = s_frameUboByHash.find(contentHash);
				if (it != s_frameUboByHash.end() && std::memcmp(s_uboRingMapped + it->second, gather, std::size_t(size)) == 0) {
					offset = it->second;
				} else {
					if (!AllocUbo(size, offset)) {
						static bool warnedRingExhausted = false;
						if (!warnedRingExhausted) {
							warnedRingExhausted = true;
							LOGW("Per-frame uniform ring exhausted ({} bytes); draws are rendered with stale uniforms", s_uboPerFrameSize);
						}
						continue;
					}
					std::memcpy(s_uboRingMapped + offset, gather, std::size_t(size));
					s_frameUboByHash[contentHash] = offset;
				}

				VkDescriptorBufferInfo& bufInfo = bufferInfos[writeCount];
				bufInfo = {};
				bufInfo.buffer = s_uboRing;
				bufInfo.offset = offset;
				bufInfo.range = size;
				w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				w.pBufferInfo = &bufInfo;
				HashU64(keyHash, 0x0b000000u | b.binding);
				HashU64(keyHash, std::uint64_t(offset));
				HashU64(keyHash, std::uint64_t(size));
				writeCount++;
			}

			// Reuse a set with the identical binding tuple, else allocate + write one and cache it (both current-frame)
			VkDescriptorSet set = VK_NULL_HANDLE;
			auto sit = s_frameSetByHash.find(keyHash);
			if (sit != s_frameSetByHash.end()) {
				set = sit->second;
			} else {
				set = AllocDescriptorSet(reinterpret_cast<VkDescriptorSetLayout>(prog->GetDescriptorSetLayout()));
				if (set == VK_NULL_HANDLE) {
					return;
				}
				for (std::uint32_t i = 0; i < writeCount; i++) {
					writes[i].dstSet = set;
				}
				if (writeCount > 0) {
					vkUpdateDescriptorSets(s_device, writeCount, writes, 0, nullptr);
				}
				s_frameSetByHash.emplace(keyHash, set);
			}
			if (s_lastDescriptorSet != set || s_lastDescriptorLayout != layout) {
				vkCmdBindDescriptorSets(s_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
				s_lastDescriptorSet = set;
				s_lastDescriptorLayout = layout;
			}
		}

		void ApplyViewportScissor()
		{
			const Recti vpRect = VulkanDevice::GetViewport();
			const VulkanDevice::ScissorState scState = VulkanDevice::GetScissorState();
			VkViewport vp = {};
			vp.x = float(vpRect.X);
			vp.y = float(vpRect.Y);
			vp.width = float(vpRect.W);
			vp.height = float(vpRect.H);
			vp.minDepth = 0.0f;
			vp.maxDepth = 1.0f;
			if (!s_lastViewportValid || std::memcmp(&s_lastViewport, &vp, sizeof(vp)) != 0) {
				vkCmdSetViewport(s_commandBuffer, 0, 1, &vp);
				s_lastViewport = vp;
				s_lastViewportValid = true;
			}

			// The framebuffer stores rows exactly like GL (bottom-up), so the GL scissor (bottom-left origin)
			// maps to the Vulkan framebuffer scissor with no flip; clamp it to the current render area.
			const VkExtent2D extent = (s_activeRenderPassTarget == nullptr) ? s_screenExtent
				: VkExtent2D{ std::uint32_t(s_activeRenderPassTarget->GetColorTexture(0) ? s_activeRenderPassTarget->GetColorTexture(0)->GetWidth() : 0),
							  std::uint32_t(s_activeRenderPassTarget->GetColorTexture(0) ? s_activeRenderPassTarget->GetColorTexture(0)->GetHeight() : 0) };
			VkRect2D sc = {};
			if (scState.Enabled && scState.Rect.W > 0 && scState.Rect.H > 0) {
				std::int32_t x = scState.Rect.X < 0 ? 0 : scState.Rect.X;
				std::int32_t y = scState.Rect.Y < 0 ? 0 : scState.Rect.Y;
				std::int32_t w = scState.Rect.W;
				std::int32_t h = scState.Rect.H;
				if (std::uint32_t(x) > extent.width) x = std::int32_t(extent.width);
				if (std::uint32_t(y) > extent.height) y = std::int32_t(extent.height);
				if (std::uint32_t(x + w) > extent.width) w = std::int32_t(extent.width) - x;
				if (std::uint32_t(y + h) > extent.height) h = std::int32_t(extent.height) - y;
				sc.offset = { x, y };
				sc.extent = { std::uint32_t(w < 0 ? 0 : w), std::uint32_t(h < 0 ? 0 : h) };
			} else {
				sc.offset = { 0, 0 };
				sc.extent = extent;
			}
			if (!s_lastScissorValid || std::memcmp(&s_lastScissor, &sc, sizeof(sc)) != 0) {
				vkCmdSetScissor(s_commandBuffer, 0, 1, &sc);
				s_lastScissor = sc;
				s_lastScissorValid = true;
			}
		}

		void DrawCommon(PrimitiveType primitive, std::int32_t firstVertex, std::uint32_t count,
			bool indexed, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex)
		{
			VulkanShaderProgram* prog = VulkanDevice::CurrentProgram();
			if (!s_ready || prog == nullptr || !prog->HasPipelineState() || count == 0) {
				return;
			}
			if (!s_frameActive) {
				BeginFrame();
			}

			// Before opening the draw's render pass (barriers + buffer->image copies must be outside a render
			// pass), for each sampled texture: (1) flush a pending CPU texel upload into this frame's command
			// buffer - this replaces the old per-frame vkQueueWaitIdle upload (e.g. the palette re-uploaded every
			// frame); (2) else bring a sampled render-target image from COLOR_ATTACHMENT to SHADER_READ_ONLY.
			// Ends the current pass once if any transition/copy is due (the LOAD-op passes preserve contents).
			for (const VulkanShaderProgram::DescriptorBinding& b : prog->GetDescriptorBindings()) {
				if (b.kind != VulkanShaderProgram::DescriptorBinding::Kind::Sampler) {
					continue;
				}
				const VulkanTexture* tex = VulkanDevice::GetBoundTexture(std::uint32_t(b.slot >= 0 ? b.slot : 0));
				if (tex == nullptr) {
					continue;
				}
				tex->EnsureGpu();
				if (tex->HasPendingUpload()) {
					EndRenderPassIfOpen();
					tex->RecordStreamingUpload(reinterpret_cast<std::uint64_t>(s_commandBuffer));
				} else if (VkImageLayout(tex->GetCurrentLayout()) == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
					EndRenderPassIfOpen();
					ImageBarrier(s_commandBuffer, reinterpret_cast<VkImage>(tex->GpuImage()),
						VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
						VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
					tex->SetCurrentLayout(std::uint32_t(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
				}
			}

			if (!EnsureRenderPass(VulkanDevice::currentRenderTargetInternal())) {
				return;
			}

			VkRenderPass renderPass = (s_activeRenderPassTarget == nullptr)
				? VkGetColorRenderPass(s_swapchainFormat)
				: VkGetColorRenderPass(VkFormat(s_activeRenderPassTarget->GetColorTexture(0)->GpuFormat()));
			VkPipeline pipeline = GetOrCreatePipeline(prog, primitive, renderPass);
			if (pipeline == VK_NULL_HANDLE) {
				return;
			}
			if (s_lastPipeline != pipeline) {
				vkCmdBindPipeline(s_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
				s_lastPipeline = pipeline;
			}
			ApplyViewportScissor();
			BuildAndBindDescriptorSet(prog);

			if (prog->HasVertexAttributes()) {
				const VulkanBufferObject* vbo = prog->GetBoundVbo();
				const std::uint64_t vk = (vbo != nullptr ? vbo->GetVkBuffer() : 0);
				if (vk == 0) {
					return;
				}
				VkBuffer vertexBuffer = reinterpret_cast<VkBuffer>(vk);
				if (s_lastVertexBuffer != vertexBuffer) {
					VkDeviceSize vboOffset = 0;
					vkCmdBindVertexBuffers(s_commandBuffer, 0, 1, &vertexBuffer, &vboOffset);
					s_lastVertexBuffer = vertexBuffer;
				}
				if (indexed) {
					const VulkanBufferObject* ibo = prog->GetBoundIbo();
					const std::uint64_t ivk = (ibo != nullptr ? ibo->GetVkBuffer() : 0);
					if (ivk == 0) {
						return;
					}
					VkBuffer indexBuffer = reinterpret_cast<VkBuffer>(ivk);
					const VkIndexType indexType = (indexFormat == IndexFormat::UInt32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
					if (s_lastIndexBuffer != indexBuffer || s_lastIndexType != indexType) {
						vkCmdBindIndexBuffer(s_commandBuffer, indexBuffer, 0, indexType);
						s_lastIndexBuffer = indexBuffer;
						s_lastIndexType = indexType;
					}
				}
			}

			const std::uint32_t instances = (numInstances > 1 ? std::uint32_t(numInstances) : 1u);
			if (indexed) {
				const std::uint32_t indexSize = (indexFormat == IndexFormat::UInt32 ? 4u : 2u);
				const std::uint32_t firstIndex = std::uint32_t(indexOffset / indexSize);
				vkCmdDrawIndexed(s_commandBuffer, count, instances, firstIndex, baseVertex, 0);
			} else {
				vkCmdDraw(s_commandBuffer, count, instances, std::uint32_t(firstVertex), 0);
			}
		}
	}

	void VulkanDevice::DrawArrays(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		DrawCommon(primitive, firstVertex, std::uint32_t(numVertices), false, IndexFormat::UInt16, 0, 1, 0);
	}
	void VulkanDevice::DrawArraysInstanced(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices, std::int32_t numInstances)
	{
		DrawCommon(primitive, firstVertex, std::uint32_t(numVertices), false, IndexFormat::UInt16, 0, numInstances, 0);
	}
	void VulkanDevice::DrawElements(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t baseVertex)
	{
		DrawCommon(primitive, 0, numIndices, true, indexFormat, indexOffset, 1, baseVertex);
	}
	void VulkanDevice::DrawElementsInstanced(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex)
	{
		DrawCommon(primitive, 0, numIndices, true, indexFormat, indexOffset, numInstances, baseVertex);
	}

	FenceHandle VulkanDevice::InsertFence() { return nullptr; }
	void VulkanDevice::DeleteFence(FenceHandle& fence) { fence = nullptr; }
	bool VulkanDevice::ClientWaitFence(FenceHandle fence, std::uint64_t timeoutNs)
	{
		static_cast<void>(fence);
		static_cast<void>(timeoutNs);
		return true;
	}

	void VulkanDevice::SetupInitialState()
	{
		blending_ = BlendingState{};
		depthTest_ = DepthTestState{};
		cullFace_ = CullFaceState{};
		scissor_ = ScissorState{};
	}

	// -- Backend extensions (recorders) --

	void VulkanDevice::BindProgram(VulkanShaderProgram* program) { currentProgram_ = program; }
	VulkanShaderProgram* VulkanDevice::CurrentProgram() { return currentProgram_; }

	void VulkanDevice::BindTexture(std::uint32_t unit, const VulkanTexture* texture)
	{
		if (unit < MaxTextureUnits) {
			boundTextures_[unit] = texture;
		}
	}

	void VulkanDevice::UnbindTexture(const VulkanTexture* texture)
	{
		for (std::uint32_t unit = 0; unit < MaxTextureUnits; unit++) {
			if (boundTextures_[unit] == texture) {
				boundTextures_[unit] = nullptr;
			}
		}
	}

	const VulkanTexture* VulkanDevice::GetBoundTexture(std::uint32_t unit)
	{
		return (unit < MaxTextureUnits ? boundTextures_[unit] : nullptr);
	}

	void VulkanDevice::BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size)
	{
		if (index < MaxUniformBindings) {
			boundUniformRanges_[index].Data = data;
			boundUniformRanges_[index].Size = size;
		}
	}

	void VulkanDevice::SetRenderTarget(VulkanRenderTarget* renderTarget) { currentRenderTarget_ = renderTarget; }

	void VulkanDevice::UnbindRenderTarget(const VulkanRenderTarget* renderTarget)
	{
		if (currentRenderTarget_ == renderTarget) {
			currentRenderTarget_ = nullptr;
		}
	}

	void VulkanDevice::OnShaderProgramDestroyed(VulkanShaderProgram* program)
	{
		if (program == nullptr || s_device == VK_NULL_HANDLE) {
			return;
		}
		if (s_device != VK_NULL_HANDLE) {
			vkDeviceWaitIdle(s_device);
		}
		const std::uint32_t handle = program->GetGLHandle();
		for (auto it = s_pipelines.begin(); it != s_pipelines.end(); ) {
			if (it->first.programHandle == handle) {
				vkDestroyPipeline(s_device, it->second, nullptr);
				it = s_pipelines.erase(it);
			} else {
				++it;
			}
		}
		if (currentProgram_ == program) {
			currentProgram_ = nullptr;
		}
	}

	// Internal accessors used by the anonymous-namespace draw helpers
	VulkanRenderTarget* VulkanDevice::currentRenderTargetInternal() { return currentRenderTarget_; }
	std::uint32_t VulkanDevice::MaxUniformBindingsPublic() { return MaxUniformBindings; }
	void VulkanDevice::GetUniformRange(std::uint32_t index, const std::uint8_t*& data, std::uint32_t& size)
	{
		if (index < MaxUniformBindings) {
			data = boundUniformRanges_[index].Data;
			size = boundUniformRanges_[index].Size;
		} else {
			data = nullptr;
			size = 0;
		}
	}

	// -- Vulkan device / swap-chain lifecycle --

	namespace
	{
		VkPhysicalDevice PickPhysicalDevice(std::uint32_t& graphicsFamily, std::uint32_t& presentFamily)
		{
			std::uint32_t count = 0;
			vkEnumeratePhysicalDevices(s_instance, &count, nullptr);
			if (count == 0) {
				LOGE("No Vulkan physical devices found");
				return VK_NULL_HANDLE;
			}
			std::vector<VkPhysicalDevice> devices(count);
			vkEnumeratePhysicalDevices(s_instance, &count, devices.data());

			VkPhysicalDevice best = VK_NULL_HANDLE;
			std::uint32_t bestGfx = 0, bestPresent = 0;
			bool bestIsDiscrete = false;

			for (VkPhysicalDevice dev : devices) {
				std::uint32_t extCount = 0;
				vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
				std::vector<VkExtensionProperties> exts(extCount);
				if (extCount > 0) {
					vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, exts.data());
				}
				bool hasSwapchain = false;
				for (const VkExtensionProperties& e : exts) {
					if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
						hasSwapchain = true;
						break;
					}
				}
				if (!hasSwapchain) {
					continue;
				}

				std::uint32_t qCount = 0;
				vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, nullptr);
				std::vector<VkQueueFamilyProperties> families(qCount);
				vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, families.data());

				std::int32_t gfx = -1, present = -1;
				for (std::uint32_t i = 0; i < qCount; i++) {
					const bool isGfx = (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
					VkBool32 canPresent = VK_FALSE;
					vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, s_surface, &canPresent);
					if (isGfx && canPresent) {
						gfx = std::int32_t(i);
						present = std::int32_t(i);
						break;
					}
					if (isGfx && gfx < 0) {
						gfx = std::int32_t(i);
					}
					if (canPresent && present < 0) {
						present = std::int32_t(i);
					}
				}
				if (gfx < 0 || present < 0) {
					continue;
				}

				std::uint32_t fmtCount = 0, pmCount = 0;
				vkGetPhysicalDeviceSurfaceFormatsKHR(dev, s_surface, &fmtCount, nullptr);
				vkGetPhysicalDeviceSurfacePresentModesKHR(dev, s_surface, &pmCount, nullptr);
				if (fmtCount == 0 || pmCount == 0) {
					continue;
				}

				VkPhysicalDeviceProperties props;
				vkGetPhysicalDeviceProperties(dev, &props);
				const bool isDiscrete = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
				if (best == VK_NULL_HANDLE || (isDiscrete && !bestIsDiscrete)) {
					best = dev;
					bestGfx = std::uint32_t(gfx);
					bestPresent = std::uint32_t(present);
					bestIsDiscrete = isDiscrete;
				}
			}

			if (best != VK_NULL_HANDLE) {
				graphicsFamily = bestGfx;
				presentFamily = bestPresent;
				VkPhysicalDeviceProperties props;
				vkGetPhysicalDeviceProperties(best, &props);
				LOGI("Vulkan physical device selected: {} (graphics family {}, present family {})", props.deviceName, bestGfx, bestPresent);
			}
			return best;
		}

		void DestroyScreenTarget()
		{
			if (s_device == VK_NULL_HANDLE) {
				return;
			}
			if (s_screenFramebuffer != VK_NULL_HANDLE) { vkDestroyFramebuffer(s_device, s_screenFramebuffer, nullptr); s_screenFramebuffer = VK_NULL_HANDLE; }
			if (s_screenView != VK_NULL_HANDLE) { vkDestroyImageView(s_device, s_screenView, nullptr); s_screenView = VK_NULL_HANDLE; }
			if (s_screenImage != VK_NULL_HANDLE) { vkDestroyImage(s_device, s_screenImage, nullptr); s_screenImage = VK_NULL_HANDLE; }
			if (s_screenMemory != VK_NULL_HANDLE) { vkFreeMemory(s_device, s_screenMemory, nullptr); s_screenMemory = VK_NULL_HANDLE; }
			s_screenLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		}

		bool CreateScreenTarget(VkExtent2D extent)
		{
			DestroyScreenTarget();
			s_screenExtent = extent;

			VkImageCreateInfo ici = {};
			ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			ici.imageType = VK_IMAGE_TYPE_2D;
			ici.format = s_swapchainFormat;
			ici.extent = { extent.width, extent.height, 1 };
			ici.mipLevels = 1;
			ici.arrayLayers = 1;
			ici.samples = VK_SAMPLE_COUNT_1_BIT;
			ici.tiling = VK_IMAGE_TILING_OPTIMAL;
			ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			if (!CheckVk(vkCreateImage(s_device, &ici, nullptr, &s_screenImage), "vkCreateImage(screen)")) {
				return false;
			}
			VkMemoryRequirements req;
			vkGetImageMemoryRequirements(s_device, s_screenImage, &req);
			VkMemoryAllocateInfo mai = {};
			mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			mai.allocationSize = req.size;
			mai.memoryTypeIndex = VkFindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			if (mai.memoryTypeIndex == UINT32_MAX || !CheckVk(vkAllocateMemory(s_device, &mai, nullptr, &s_screenMemory), "vkAllocateMemory(screen)")) {
				return false;
			}
			vkBindImageMemory(s_device, s_screenImage, s_screenMemory, 0);

			VkImageViewCreateInfo ivci = {};
			ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			ivci.image = s_screenImage;
			ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
			ivci.format = s_swapchainFormat;
			ivci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			if (!CheckVk(vkCreateImageView(s_device, &ivci, nullptr, &s_screenView), "vkCreateImageView(screen)")) {
				return false;
			}

			VkFramebufferCreateInfo fci = {};
			fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fci.renderPass = VkGetColorRenderPass(s_swapchainFormat);
			fci.attachmentCount = 1;
			fci.pAttachments = &s_screenView;
			fci.width = extent.width;
			fci.height = extent.height;
			fci.layers = 1;
			if (!CheckVk(vkCreateFramebuffer(s_device, &fci, nullptr, &s_screenFramebuffer), "vkCreateFramebuffer(screen)")) {
				return false;
			}

			// Bring it to COLOR_ATTACHMENT_OPTIMAL so the LOAD-op render passes are valid from the first frame
			VkCommandBuffer cmd = VkBeginOneTimeCommands();
			if (cmd != VK_NULL_HANDLE) {
				ImageBarrier(cmd, s_screenImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
				VkEndOneTimeCommands(cmd);
				s_screenLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			}
			return true;
		}

		bool CreateDummyTexture()
		{
			VkImageCreateInfo ici = {};
			ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			ici.imageType = VK_IMAGE_TYPE_2D;
			ici.format = VK_FORMAT_R8G8B8A8_UNORM;
			ici.extent = { 1, 1, 1 };
			ici.mipLevels = 1;
			ici.arrayLayers = 1;
			ici.samples = VK_SAMPLE_COUNT_1_BIT;
			ici.tiling = VK_IMAGE_TILING_OPTIMAL;
			ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			if (vkCreateImage(s_device, &ici, nullptr, &s_dummyImage) != VK_SUCCESS) {
				return false;
			}
			VkMemoryRequirements req;
			vkGetImageMemoryRequirements(s_device, s_dummyImage, &req);
			VkMemoryAllocateInfo mai = {};
			mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			mai.allocationSize = req.size;
			mai.memoryTypeIndex = VkFindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			if (mai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(s_device, &mai, nullptr, &s_dummyMemory) != VK_SUCCESS) {
				return false;
			}
			vkBindImageMemory(s_device, s_dummyImage, s_dummyMemory, 0);

			VkCommandBuffer cmd = VkBeginOneTimeCommands();
			if (cmd != VK_NULL_HANDLE) {
				ImageBarrier(cmd, s_dummyImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, VK_ACCESS_SHADER_READ_BIT);
				VkEndOneTimeCommands(cmd);
			}

			VkImageViewCreateInfo ivci = {};
			ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			ivci.image = s_dummyImage;
			ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
			ivci.format = VK_FORMAT_R8G8B8A8_UNORM;
			ivci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			vkCreateImageView(s_device, &ivci, nullptr, &s_dummyView);

			VkSamplerCreateInfo sci = {};
			sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			sci.magFilter = VK_FILTER_NEAREST;
			sci.minFilter = VK_FILTER_NEAREST;
			sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			vkCreateSampler(s_device, &sci, nullptr, &s_dummySampler);
			return true;
		}

		// -- Synchronization-object lifecycle --
		// All sync objects are (re)created together with the swap chain (from CreateSwapchainObjects, after a
		// vkDeviceWaitIdle) and torn down in DestroySwapchainObjects, so a resize NEVER carries a binary semaphore
		// across in a signalled state - the core resize-freeze fix. The per-frame image-available semaphore + fence
		// are recreated too (belt and braces: guarantees no stale signalled state survives the resize).

		void DestroyFrameSyncObjects()
		{
			for (FrameData& fr : s_frames) {
				if (fr.ImageAvailable != VK_NULL_HANDLE) { vkDestroySemaphore(s_device, fr.ImageAvailable, nullptr); fr.ImageAvailable = VK_NULL_HANDLE; }
				if (fr.InFlightFence != VK_NULL_HANDLE) { vkDestroyFence(s_device, fr.InFlightFence, nullptr); fr.InFlightFence = VK_NULL_HANDLE; }
			}
		}

		bool CreateFrameSyncObjects()
		{
			VkSemaphoreCreateInfo semInfo = {};
			semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			VkFenceCreateInfo fenceInfo = {};
			fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;	// so the first BeginFrame wait returns immediately
			for (FrameData& fr : s_frames) {
				if (!CheckVk(vkCreateSemaphore(s_device, &semInfo, nullptr, &fr.ImageAvailable), "vkCreateSemaphore(imageAvailable)") ||
					!CheckVk(vkCreateFence(s_device, &fenceInfo, nullptr, &fr.InFlightFence), "vkCreateFence(inFlight)")) {
					return false;
				}
				fr.FenceInFlight = false;	// fresh fence, nothing submitted against it yet
			}
			return true;
		}

		void DestroySwapchainSyncObjects()
		{
			for (VkSemaphore sem : s_renderFinishedPerImage) {
				if (sem != VK_NULL_HANDLE) { vkDestroySemaphore(s_device, sem, nullptr); }
			}
			s_renderFinishedPerImage.clear();
			s_imagesInFlight.clear();
		}

		bool CreateSwapchainSyncObjects(std::uint32_t imageCount)
		{
			VkSemaphoreCreateInfo semInfo = {};
			semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			s_renderFinishedPerImage.assign(imageCount, VK_NULL_HANDLE);
			for (std::uint32_t i = 0; i < imageCount; i++) {
				if (!CheckVk(vkCreateSemaphore(s_device, &semInfo, nullptr, &s_renderFinishedPerImage[i]), "vkCreateSemaphore(renderFinished)")) {
					return false;
				}
			}
			s_imagesInFlight.assign(imageCount, VK_NULL_HANDLE);	// no owned fences; tracks the in-flight fence per image
			return true;
		}

		void DestroySwapchainObjects()
		{
			if (s_device == VK_NULL_HANDLE) {
				return;
			}
			// Callers (ResizeSwapchain / DestroySwapchain) have already waited the device idle, so all sync objects
			// and queued staging buffers are safe to destroy here.
			DestroyFrameSyncObjects();
			DestroySwapchainSyncObjects();
			for (FrameData& fr : s_frames) {
				FreeFrameStaging(fr);
			}
			for (VkImageView view : s_swapchainImageViews) {
				if (view != VK_NULL_HANDLE) {
					vkDestroyImageView(s_device, view, nullptr);
				}
			}
			s_swapchainImageViews.clear();
			s_swapchainImages.clear();
			if (s_swapchain != VK_NULL_HANDLE) {
				vkDestroySwapchainKHR(s_device, s_swapchain, nullptr);
				s_swapchain = VK_NULL_HANDLE;
			}
		}

		bool CreateSwapchainObjects(std::int32_t width, std::int32_t height)
		{
			VkSurfaceCapabilitiesKHR caps;
			if (!CheckVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(s_physicalDevice, s_surface, &caps), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) {
				return false;
			}

			std::uint32_t fmtCount = 0;
			vkGetPhysicalDeviceSurfaceFormatsKHR(s_physicalDevice, s_surface, &fmtCount, nullptr);
			std::vector<VkSurfaceFormatKHR> formats(fmtCount);
			vkGetPhysicalDeviceSurfaceFormatsKHR(s_physicalDevice, s_surface, &fmtCount, formats.data());
			VkSurfaceFormatKHR chosenFormat = formats.empty() ? VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } : formats[0];
			for (const VkSurfaceFormatKHR& f : formats) {
				if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
					f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
					chosenFormat = f;
					break;
				}
			}

			VkPresentModeKHR chosenPresent = VK_PRESENT_MODE_FIFO_KHR;
			if (!s_vsync) {
				std::uint32_t pmCount = 0;
				vkGetPhysicalDeviceSurfacePresentModesKHR(s_physicalDevice, s_surface, &pmCount, nullptr);
				std::vector<VkPresentModeKHR> modes(pmCount);
				vkGetPhysicalDeviceSurfacePresentModesKHR(s_physicalDevice, s_surface, &pmCount, modes.data());
				for (VkPresentModeKHR m : modes) {
					if (m == VK_PRESENT_MODE_MAILBOX_KHR) { chosenPresent = m; break; }
					if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) { chosenPresent = VK_PRESENT_MODE_IMMEDIATE_KHR; }
				}
			}

			VkExtent2D extent;
			if (caps.currentExtent.width != 0xFFFFFFFFu) {
				extent = caps.currentExtent;
			} else {
				extent.width = std::uint32_t(width < 1 ? 1 : width);
				extent.height = std::uint32_t(height < 1 ? 1 : height);
				if (extent.width < caps.minImageExtent.width) extent.width = caps.minImageExtent.width;
				if (extent.width > caps.maxImageExtent.width) extent.width = caps.maxImageExtent.width;
				if (extent.height < caps.minImageExtent.height) extent.height = caps.minImageExtent.height;
				if (extent.height > caps.maxImageExtent.height) extent.height = caps.maxImageExtent.height;
			}
			if (extent.width == 0 || extent.height == 0) {
				return false;
			}

			std::uint32_t imageCount = caps.minImageCount + 1;
			if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
				imageCount = caps.maxImageCount;
			}

			// The present blit needs TRANSFER_DST on the swap-chain images
			VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
			if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
				usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			} else {
				LOGW("Swap-chain images lack TRANSFER_DST usage; the present blit may fail");
			}

			VkSwapchainCreateInfoKHR sci = {};
			sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
			sci.surface = s_surface;
			sci.minImageCount = imageCount;
			sci.imageFormat = chosenFormat.format;
			sci.imageColorSpace = chosenFormat.colorSpace;
			sci.imageExtent = extent;
			sci.imageArrayLayers = 1;
			sci.imageUsage = usage;
			const std::uint32_t families[2] = { s_graphicsFamily, s_presentFamily };
			if (s_graphicsFamily != s_presentFamily) {
				sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
				sci.queueFamilyIndexCount = 2;
				sci.pQueueFamilyIndices = families;
			} else {
				sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			}
			sci.preTransform = caps.currentTransform;
			sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
			sci.presentMode = chosenPresent;
			sci.clipped = VK_TRUE;
			sci.oldSwapchain = VK_NULL_HANDLE;

			if (!CheckVk(vkCreateSwapchainKHR(s_device, &sci, nullptr, &s_swapchain), "vkCreateSwapchainKHR")) {
				return false;
			}
			s_swapchainFormat = chosenFormat.format;
			s_swapchainExtent = extent;

			std::uint32_t realCount = 0;
			vkGetSwapchainImagesKHR(s_device, s_swapchain, &realCount, nullptr);
			s_swapchainImages.resize(realCount);
			vkGetSwapchainImagesKHR(s_device, s_swapchain, &realCount, s_swapchainImages.data());
			s_swapchainImageViews.assign(realCount, VK_NULL_HANDLE);	// views unused (blit target), kept for symmetry

			// (Re)create all sync objects fresh alongside the swap chain (the device is idle here), so no binary
			// semaphore survives a resize in a signalled state and renderFinished matches the new image count.
			if (!CreateFrameSyncObjects() || !CreateSwapchainSyncObjects(realCount)) {
				return false;
			}
			s_currentFrame = 0;
			for (FrameData& fr : s_frames) {
				fr.UboCursor = 0;
				fr.DescPoolCursor = 0;
			}

			if (!CreateScreenTarget(extent)) {
				return false;
			}

			LOGI("Vulkan swap chain created: {}x{}, {} images, format {}, {} frames in flight", extent.width, extent.height, realCount, static_cast<std::int32_t>(s_swapchainFormat), MaxFramesInFlight);
			return true;
		}
	}

	bool VulkanDevice::CreateSwapchain(void* windowHandle, std::int32_t width, std::int32_t height, bool vsync)
	{
#if defined(WITH_SDL)
		s_vsync = vsync;
		s_sdlWindow = windowHandle;
		SDL_Window* window = reinterpret_cast<SDL_Window*>(windowHandle);

		if (SDL_Vulkan_LoadLibrary(nullptr) != 0) {
			LOGE("SDL_Vulkan_LoadLibrary failed: {}", SDL_GetError());
			return false;
		}
		vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
		if (vkGetInstanceProcAddr == nullptr) {
			LOGE("SDL_Vulkan_GetVkGetInstanceProcAddr returned null");
			return false;
		}
		LoadGlobalFunctions();
		if (vkCreateInstance == nullptr) {
			LOGE("Failed to load vkCreateInstance from the Vulkan loader");
			return false;
		}

		std::uint32_t sdlExtCount = 0;
		if (!SDL_Vulkan_GetInstanceExtensions(window, &sdlExtCount, nullptr)) {
			LOGE("SDL_Vulkan_GetInstanceExtensions (count) failed: {}", SDL_GetError());
			return false;
		}
		std::vector<const char*> instanceExts(sdlExtCount);
		if (sdlExtCount > 0 && !SDL_Vulkan_GetInstanceExtensions(window, &sdlExtCount, instanceExts.data())) {
			LOGE("SDL_Vulkan_GetInstanceExtensions (names) failed: {}", SDL_GetError());
			return false;
		}

		std::uint32_t availExtCount = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &availExtCount, nullptr);
		std::vector<VkExtensionProperties> availExts(availExtCount);
		if (availExtCount > 0) {
			vkEnumerateInstanceExtensionProperties(nullptr, &availExtCount, availExts.data());
		}
		auto hasInstanceExt = [&](const char* name) {
			for (const VkExtensionProperties& e : availExts) {
				if (std::strcmp(e.extensionName, name) == 0) return true;
			}
			return false;
		};
		bool hasValidationLayer = false;
		{
			std::uint32_t layerCount = 0;
			vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
			std::vector<VkLayerProperties> layers(layerCount);
			if (layerCount > 0) {
				vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
			}
			for (const VkLayerProperties& l : layers) {
				if (std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0) { hasValidationLayer = true; break; }
			}
		}
		const bool wantValidation = hasValidationLayer && hasInstanceExt(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		if (wantValidation) {
			instanceExts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}
		const char* validationLayer = "VK_LAYER_KHRONOS_validation";

		VkApplicationInfo appInfo = {};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "Jazz2";
		appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.pEngineName = "nCine";
		appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.apiVersion = VK_API_VERSION_1_0;

		VkInstanceCreateInfo ici = {};
		ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		ici.pApplicationInfo = &appInfo;
		ici.enabledExtensionCount = std::uint32_t(instanceExts.size());
		ici.ppEnabledExtensionNames = instanceExts.empty() ? nullptr : instanceExts.data();
		if (wantValidation) {
			ici.enabledLayerCount = 1;
			ici.ppEnabledLayerNames = &validationLayer;
			LOGI("Vulkan validation layer VK_LAYER_KHRONOS_validation enabled");
		}

		if (!CheckVk(vkCreateInstance(&ici, nullptr, &s_instance), "vkCreateInstance")) {
			return false;
		}
		LoadInstanceFunctions();

		if (wantValidation && vkCreateDebugUtilsMessengerEXT != nullptr) {
			VkDebugUtilsMessengerCreateInfoEXT dci = {};
			dci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			dci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			dci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			dci.pfnUserCallback = DebugCallback;
			vkCreateDebugUtilsMessengerEXT(s_instance, &dci, nullptr, &s_debugMessenger);
		}

		if (!SDL_Vulkan_CreateSurface(window, s_instance, &s_surface)) {
			LOGE("SDL_Vulkan_CreateSurface failed: {}", SDL_GetError());
			DestroySwapchain();
			return false;
		}

		s_physicalDevice = PickPhysicalDevice(s_graphicsFamily, s_presentFamily);
		if (s_physicalDevice == VK_NULL_HANDLE) {
			LOGE("No suitable Vulkan physical device (needs graphics+present queue and VK_KHR_swapchain)");
			DestroySwapchain();
			return false;
		}
		vkGetPhysicalDeviceMemoryProperties(s_physicalDevice, &s_memProps);
		{
			VkPhysicalDeviceProperties props;
			vkGetPhysicalDeviceProperties(s_physicalDevice, &props);
			s_minUboAlign = props.limits.minUniformBufferOffsetAlignment;
			if (s_minUboAlign == 0) {
				s_minUboAlign = 16;
			}
			VkPhysicalDeviceFeatures feats;
			vkGetPhysicalDeviceFeatures(s_physicalDevice, &feats);
			s_depthClamp = (feats.depthClamp == VK_TRUE);
		}

		const float queuePriority = 1.0f;
		VkDeviceQueueCreateInfo queueInfos[2] = {};
		std::uint32_t queueInfoCount = 1;
		queueInfos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueInfos[0].queueFamilyIndex = s_graphicsFamily;
		queueInfos[0].queueCount = 1;
		queueInfos[0].pQueuePriorities = &queuePriority;
		if (s_graphicsFamily != s_presentFamily) {
			queueInfos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueInfos[1].queueFamilyIndex = s_presentFamily;
			queueInfos[1].queueCount = 1;
			queueInfos[1].pQueuePriorities = &queuePriority;
			queueInfoCount = 2;
		}

		VkPhysicalDeviceFeatures enabledFeatures = {};
		enabledFeatures.depthClamp = s_depthClamp ? VK_TRUE : VK_FALSE;

		const char* deviceExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
		VkDeviceCreateInfo dci = {};
		dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		dci.queueCreateInfoCount = queueInfoCount;
		dci.pQueueCreateInfos = queueInfos;
		dci.enabledExtensionCount = 1;
		dci.ppEnabledExtensionNames = deviceExts;
		dci.pEnabledFeatures = &enabledFeatures;
		if (!CheckVk(vkCreateDevice(s_physicalDevice, &dci, nullptr, &s_device), "vkCreateDevice")) {
			DestroySwapchain();
			return false;
		}
		LoadDeviceFunctions();

		vkGetDeviceQueue(s_device, s_graphicsFamily, 0, &s_graphicsQueue);
		vkGetDeviceQueue(s_device, s_presentFamily, 0, &s_presentQueue);

		// Command pool + one primary command buffer (re-recorded each frame)
		VkCommandPoolCreateInfo pci = {};
		pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		pci.queueFamilyIndex = s_graphicsFamily;
		if (!CheckVk(vkCreateCommandPool(s_device, &pci, nullptr, &s_commandPool), "vkCreateCommandPool")) {
			DestroySwapchain();
			return false;
		}
		// One primary command buffer PER in-flight frame (re-recorded when its slot is entered);
		// RESET_COMMAND_BUFFER on the pool lets each be reset independently while the other is in flight.
		for (FrameData& fr : s_frames) {
			VkCommandBufferAllocateInfo cbai = {};
			cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			cbai.commandPool = s_commandPool;
			cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			cbai.commandBufferCount = 1;
			if (!CheckVk(vkAllocateCommandBuffers(s_device, &cbai, &fr.CommandBuffer), "vkAllocateCommandBuffers")) {
				DestroySwapchain();
				return false;
			}
		}
		s_commandBuffer = s_frames[0].CommandBuffer;

		// The image-available semaphores + in-flight fences (per frame) and render-finished semaphores (per
		// swap-chain image) are created in CreateSwapchainObjects, so a resize refreshes them with the swap chain.

		// Uniform ring (host-visible, persistently mapped), split into one disjoint region per in-flight frame
		s_uboRingSize = 16u * 1024u * 1024u;
		s_uboPerFrameSize = s_uboRingSize / MaxFramesInFlight;
		{
			VkBufferCreateInfo bci = {};
			bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bci.size = s_uboRingSize;
			bci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			if (!CheckVk(vkCreateBuffer(s_device, &bci, nullptr, &s_uboRing), "vkCreateBuffer(uboRing)")) {
				DestroySwapchain();
				return false;
			}
			VkMemoryRequirements req;
			vkGetBufferMemoryRequirements(s_device, s_uboRing, &req);
			VkMemoryAllocateInfo mai = {};
			mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			mai.allocationSize = req.size;
			mai.memoryTypeIndex = VkFindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			if (mai.memoryTypeIndex == UINT32_MAX || !CheckVk(vkAllocateMemory(s_device, &mai, nullptr, &s_uboRingMem), "vkAllocateMemory(uboRing)")) {
				DestroySwapchain();
				return false;
			}
			vkBindBufferMemory(s_device, s_uboRing, s_uboRingMem, 0);
			void* mapped = nullptr;
			vkMapMemory(s_device, s_uboRingMem, 0, VK_WHOLE_SIZE, 0, &mapped);
			s_uboRingMapped = static_cast<std::uint8_t*>(mapped);
		}

		CreateDummyTexture();

		if (!CreateSwapchainObjects(width, height)) {
			DestroySwapchain();
			return false;
		}

		if (width > 0 && height > 0) {
			SetViewport(Recti(0, 0, width, height));
		}
		s_ready = true;
		LOGI("Vulkan device ready (real render pipeline; depthClamp {}, minUboAlign {})",
			s_depthClamp ? "on" : "off", static_cast<std::int32_t>(s_minUboAlign));
		return true;
#else
		static_cast<void>(windowHandle);
		static_cast<void>(width);
		static_cast<void>(height);
		static_cast<void>(vsync);
		LOGE("The Vulkan backend requires the SDL2 window backend");
		return false;
#endif
	}

	namespace
	{
		// Current Vulkan drawable size of the SDL window (0x0 while minimized). Used by the present/acquire
		// out-of-date recovery so the swap chain is always rebuilt at the true new size (correct even on platforms
		// whose surface caps report 0xFFFFFFFF, where the stale s_swapchainExtent would have been wrong).
		void QueryDrawableSize(std::int32_t& outW, std::int32_t& outH)
		{
			outW = 0;
			outH = 0;
#if defined(WITH_SDL)
			if (s_sdlWindow != nullptr) {
				int w = 0, h = 0;
				SDL_Vulkan_GetDrawableSize(reinterpret_cast<SDL_Window*>(s_sdlWindow), &w, &h);
				outW = w;
				outH = h;
			}
#endif
		}
	}

	void VulkanDevice::ResizeSwapchain(std::int32_t width, std::int32_t height)
	{
		if (s_device == VK_NULL_HANDLE) {
			return;
		}
		// Idle first so the swap chain + ALL sync objects can be safely destroyed and recreated (DestroySwapchainObjects
		// tears down the per-frame + per-image semaphores/fences, so none survives the resize in a signalled state).
		vkDeviceWaitIdle(s_device);
		s_frameActive = false;
		s_renderPassOpen = false;
		DestroySwapchainObjects();
		// The device is idle, so every frame that could have referenced a deferred-destroy resource has completed;
		// free them now rather than carrying partial fence bitmasks across the swap-chain recreation.
		FlushDeferredDestroys();
		if (width <= 0 || height <= 0) {
			// Minimized / zero-size surface: stay not-ready without recreating. PresentFrame polls the drawable
			// size each frame and recreates once the window is restored (no wedge, no hang).
			s_ready = false;
			return;
		}
		if (CreateSwapchainObjects(width, height)) {
			SetViewport(Recti(0, 0, width, height));
			s_ready = true;
		} else {
			s_ready = false;
		}
	}

	void VulkanDevice::PresentFrame()
	{
		if (s_device == VK_NULL_HANDLE) {
			return;
		}
		// Recover a lost / zero-size swap chain (e.g. after a minimize, or an out-of-date resize that left us
		// not-ready): try to recreate at the window's current drawable size; while it is still 0x0 (minimized)
		// drop the frame instead of blocking. This is what lets minimize -> restore recover cleanly.
		if (!s_ready || s_swapchain == VK_NULL_HANDLE) {
			std::int32_t dw = 0, dh = 0;
			QueryDrawableSize(dw, dh);
			if (dw > 0 && dh > 0) {
				ResizeSwapchain(dw, dh);
			}
			if (!s_ready || s_swapchain == VK_NULL_HANDLE) {
				if (s_frameActive) {
					vkEndCommandBuffer(s_commandBuffer);
					s_frameActive = false;
				}
				return;
			}
		}

		// A frame with no draws still presents a (black) screen image
		if (!s_frameActive) {
			BeginFrame();
			if (!s_frameActive) {
				return;
			}
		}

		EndRenderPassIfOpen();

		FrameData& fr = s_frames[s_currentFrame];
		std::uint32_t imageIndex = 0;
		VkResult acquire = vkAcquireNextImageKHR(s_device, s_swapchain, UINT64_MAX, fr.ImageAvailable, VK_NULL_HANDLE, &imageIndex);
		if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
			// Acquire failed => fr.ImageAvailable was NOT signalled and nothing was submitted, so no binary
			// semaphore is left dangling. Abandon the recording and recreate at the true current size.
			vkEndCommandBuffer(s_commandBuffer);
			s_frameActive = false;
			std::int32_t dw = 0, dh = 0;
			QueryDrawableSize(dw, dh);
			ResizeSwapchain(dw, dh);
			return;
		}
		if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
			vkEndCommandBuffer(s_commandBuffer);
			s_frameActive = false;
			return;
		}
		// SUBOPTIMAL falls through: the image WAS acquired (fr.ImageAvailable is signalled), so we must submit to
		// consume it, then recreate after present.

		// If a previous in-flight frame still targets this image, wait its fence before we reuse the image
		// (standard imagesInFlight guard). Then record that this image is now owned by this frame's fence.
		if (s_imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
			vkWaitForFences(s_device, 1, &s_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
		}
		s_imagesInFlight[imageIndex] = fr.InFlightFence;

		VkImage swapImage = s_swapchainImages[imageIndex];

		// screen (COLOR_ATTACHMENT) -> TRANSFER_SRC; swap-chain image (UNDEFINED) -> TRANSFER_DST
		ImageBarrier(s_commandBuffer, s_screenImage, s_screenLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
		s_screenLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		ImageBarrier(s_commandBuffer, swapImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);

		// Vertically-flipped blit: the scene is stored GL-bottom-up, so map screen row 0 (game bottom) to the
		// swap-chain's bottom row and screen top to row 0 - the single GL->Vulkan scan-out correction.
		VkImageBlit blit = {};
		blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		blit.srcOffsets[0] = { 0, 0, 0 };
		blit.srcOffsets[1] = { std::int32_t(s_screenExtent.width), std::int32_t(s_screenExtent.height), 1 };
		blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		blit.dstOffsets[0] = { 0, std::int32_t(s_swapchainExtent.height), 0 };
		blit.dstOffsets[1] = { std::int32_t(s_swapchainExtent.width), 0, 1 };
		vkCmdBlitImage(s_commandBuffer, s_screenImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			swapImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);

		// swap-chain image -> PRESENT_SRC; screen -> COLOR_ATTACHMENT for the next frame
		ImageBarrier(s_commandBuffer, swapImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
		ImageBarrier(s_commandBuffer, s_screenImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
		s_screenLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		vkEndCommandBuffer(s_commandBuffer);

		// Submit waits on THIS frame's image-available semaphore and signals the PER-IMAGE render-finished
		// semaphore (never a per-frame one), which the present then waits on. A binary semaphore is therefore
		// never re-signalled before the present that consumes it has run (the image is not re-acquired until its
		// present has drained render-finished), so the resize / out-of-date semaphore-reuse deadlock cannot occur.
		VkSemaphore renderFinished = s_renderFinishedPerImage[imageIndex];
		const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		VkSubmitInfo submit = {};
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.waitSemaphoreCount = 1;
		submit.pWaitSemaphores = &fr.ImageAvailable;
		submit.pWaitDstStageMask = &waitStage;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &s_commandBuffer;
		submit.signalSemaphoreCount = 1;
		submit.pSignalSemaphores = &renderFinished;
		vkResetFences(s_device, 1, &fr.InFlightFence);
		s_frameActive = false;
		if (vkQueueSubmit(s_graphicsQueue, 1, &submit, fr.InFlightFence) != VK_SUCCESS) {
			// Submit failed: the fence was reset but never enqueued, so it will never signal - FenceInFlight stays
			// false so the next BeginFrame does not deadlock waiting on it, and the image-in-flight entry is
			// cleared so a future acquire of this image does not wait on the same never-signalled fence.
			// fr.ImageAvailable stays signalled, but the next successful submit for this slot consumes it
			// (or the next resize recreates it).
			s_imagesInFlight[imageIndex] = VK_NULL_HANDLE;
			LOGE("vkQueueSubmit() failed, dropping the frame");
			return;
		}
		fr.FenceInFlight = true;

		VkPresentInfoKHR present = {};
		present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present.waitSemaphoreCount = 1;
		present.pWaitSemaphores = &renderFinished;
		present.swapchainCount = 1;
		present.pSwapchains = &s_swapchain;
		present.pImageIndices = &imageIndex;
		VkResult res = vkQueuePresentKHR(s_presentQueue, &present);

		// Advance to the next in-flight slot (round-robin) now that this frame's fence/semaphores are in flight
		s_currentFrame = (s_currentFrame + 1) % MaxFramesInFlight;

		if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
			// The recreate destroys + rebuilds every sync object, so any render-finished semaphore left signalled
			// by an out-of-date present is discarded rather than re-signalled next frame.
			std::int32_t dw = 0, dh = 0;
			QueryDrawableSize(dw, dh);
			ResizeSwapchain(dw, dh);
		}
	}

	void VulkanDevice::DestroySwapchain()
	{
		if (s_device != VK_NULL_HANDLE) {
			vkDeviceWaitIdle(s_device);
		}
		s_ready = false;
		s_frameActive = false;
		s_renderPassOpen = false;

		if (s_device != VK_NULL_HANDLE) {
			for (auto& kv : s_pipelines) {
				vkDestroyPipeline(s_device, kv.second, nullptr);
			}
			s_pipelines.clear();
			for (auto& kv : s_renderPasses) {
				vkDestroyRenderPass(s_device, kv.second, nullptr);
			}
			s_renderPasses.clear();
			// Per-frame descriptor pools + any queued staging buffers (device is idle above)
			for (FrameData& fr : s_frames) {
				for (VkDescriptorPool pool : fr.DescPools) {
					vkDestroyDescriptorPool(s_device, pool, nullptr);
				}
				fr.DescPools.clear();
				fr.DescPoolCursor = 0;
				FreeFrameStaging(fr);
			}
			// Flush any resources still deferred for destruction (device is idle above, so all are safe to free);
			// otherwise their handles would leak past the vkDestroyDevice below.
			FlushDeferredDestroys();

			if (s_dummySampler != VK_NULL_HANDLE) { vkDestroySampler(s_device, s_dummySampler, nullptr); s_dummySampler = VK_NULL_HANDLE; }
			if (s_dummyView != VK_NULL_HANDLE) { vkDestroyImageView(s_device, s_dummyView, nullptr); s_dummyView = VK_NULL_HANDLE; }
			if (s_dummyImage != VK_NULL_HANDLE) { vkDestroyImage(s_device, s_dummyImage, nullptr); s_dummyImage = VK_NULL_HANDLE; }
			if (s_dummyMemory != VK_NULL_HANDLE) { vkFreeMemory(s_device, s_dummyMemory, nullptr); s_dummyMemory = VK_NULL_HANDLE; }

			if (s_uboRingMapped != nullptr) { vkUnmapMemory(s_device, s_uboRingMem); s_uboRingMapped = nullptr; }
			if (s_uboRing != VK_NULL_HANDLE) { vkDestroyBuffer(s_device, s_uboRing, nullptr); s_uboRing = VK_NULL_HANDLE; }
			if (s_uboRingMem != VK_NULL_HANDLE) { vkFreeMemory(s_device, s_uboRingMem, nullptr); s_uboRingMem = VK_NULL_HANDLE; }

			DestroyScreenTarget();

			// The swap chain AND all sync objects (per-frame image-available/fence, per-image render-finished) are
			// torn down here; destroying the command pool frees every per-frame command buffer.
			DestroySwapchainObjects();
			if (s_commandPool != VK_NULL_HANDLE) {
				vkDestroyCommandPool(s_device, s_commandPool, nullptr);
				s_commandPool = VK_NULL_HANDLE;
				for (FrameData& fr : s_frames) {
					fr.CommandBuffer = VK_NULL_HANDLE;
				}
				s_commandBuffer = VK_NULL_HANDLE;
			}
			if (s_pipelineCache != VK_NULL_HANDLE) {
				// Kept across swapchain rebuilds (so resized pipelines reuse the driver cache); dies with the device
				if (vkDestroyPipelineCache != nullptr) {
					vkDestroyPipelineCache(s_device, s_pipelineCache, nullptr);
				}
				s_pipelineCache = VK_NULL_HANDLE;
			}
			vkDestroyDevice(s_device, nullptr);
			s_device = VK_NULL_HANDLE;
		}
		if (s_instance != VK_NULL_HANDLE) {
			if (s_surface != VK_NULL_HANDLE && vkDestroySurfaceKHR != nullptr) {
				vkDestroySurfaceKHR(s_instance, s_surface, nullptr);
				s_surface = VK_NULL_HANDLE;
			}
			if (s_debugMessenger != VK_NULL_HANDLE && vkDestroyDebugUtilsMessengerEXT != nullptr) {
				vkDestroyDebugUtilsMessengerEXT(s_instance, s_debugMessenger, nullptr);
				s_debugMessenger = VK_NULL_HANDLE;
			}
			vkDestroyInstance(s_instance, nullptr);
			s_instance = VK_NULL_HANDLE;
		}
		s_physicalDevice = VK_NULL_HANDLE;
		s_graphicsQueue = VK_NULL_HANDLE;
		s_presentQueue = VK_NULL_HANDLE;
	}

	// -- Loader --

	namespace
	{
		VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
			VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
		{
			if (data == nullptr || data->pMessage == nullptr) {
				return VK_FALSE;
			}
			if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
				LOGE("Vulkan: {}", data->pMessage);
			} else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
				LOGW("Vulkan: {}", data->pMessage);
			}
			return VK_FALSE;
		}
	}

	void VulkanDevice::LoadGlobalFunctions()
	{
		vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(vkGetInstanceProcAddr(nullptr, "vkCreateInstance"));
		vkEnumerateInstanceLayerProperties = reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceLayerProperties"));
		vkEnumerateInstanceExtensionProperties = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceExtensionProperties"));
	}

	void VulkanDevice::LoadInstanceFunctions()
	{
#define VK_LOAD_INSTANCE(name) name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(s_instance, #name))
		VK_LOAD_INSTANCE(vkDestroyInstance);
		VK_LOAD_INSTANCE(vkEnumeratePhysicalDevices);
		VK_LOAD_INSTANCE(vkGetPhysicalDeviceProperties);
		VK_LOAD_INSTANCE(vkGetPhysicalDeviceFeatures);
		VK_LOAD_INSTANCE(vkGetPhysicalDeviceMemoryProperties);
		VK_LOAD_INSTANCE(vkGetPhysicalDeviceFormatProperties);
		VK_LOAD_INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);
		VK_LOAD_INSTANCE(vkEnumerateDeviceExtensionProperties);
		VK_LOAD_INSTANCE(vkGetPhysicalDeviceSurfaceSupportKHR);
		VK_LOAD_INSTANCE(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
		VK_LOAD_INSTANCE(vkGetPhysicalDeviceSurfaceFormatsKHR);
		VK_LOAD_INSTANCE(vkGetPhysicalDeviceSurfacePresentModesKHR);
		VK_LOAD_INSTANCE(vkCreateDevice);
		VK_LOAD_INSTANCE(vkGetDeviceProcAddr);
		VK_LOAD_INSTANCE(vkDestroySurfaceKHR);
		VK_LOAD_INSTANCE(vkCreateDebugUtilsMessengerEXT);
		VK_LOAD_INSTANCE(vkDestroyDebugUtilsMessengerEXT);
#undef VK_LOAD_INSTANCE
	}

	void VulkanDevice::LoadDeviceFunctions()
	{
#define VK_LOAD_DEVICE(name) name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(s_device, #name))
		VK_LOAD_DEVICE(vkDestroyDevice);
		VK_LOAD_DEVICE(vkDeviceWaitIdle);
		VK_LOAD_DEVICE(vkGetDeviceQueue);
		VK_LOAD_DEVICE(vkCreateSwapchainKHR);
		VK_LOAD_DEVICE(vkDestroySwapchainKHR);
		VK_LOAD_DEVICE(vkGetSwapchainImagesKHR);
		VK_LOAD_DEVICE(vkCreateImageView);
		VK_LOAD_DEVICE(vkDestroyImageView);
		VK_LOAD_DEVICE(vkCreateCommandPool);
		VK_LOAD_DEVICE(vkDestroyCommandPool);
		VK_LOAD_DEVICE(vkAllocateCommandBuffers);
		VK_LOAD_DEVICE(vkFreeCommandBuffers);
		VK_LOAD_DEVICE(vkResetCommandBuffer);
		VK_LOAD_DEVICE(vkBeginCommandBuffer);
		VK_LOAD_DEVICE(vkEndCommandBuffer);
		VK_LOAD_DEVICE(vkCmdClearColorImage);
		VK_LOAD_DEVICE(vkCmdPipelineBarrier);
		VK_LOAD_DEVICE(vkCreateSemaphore);
		VK_LOAD_DEVICE(vkDestroySemaphore);
		VK_LOAD_DEVICE(vkCreateFence);
		VK_LOAD_DEVICE(vkDestroyFence);
		VK_LOAD_DEVICE(vkWaitForFences);
		VK_LOAD_DEVICE(vkResetFences);
		VK_LOAD_DEVICE(vkAcquireNextImageKHR);
		VK_LOAD_DEVICE(vkQueueSubmit);
		VK_LOAD_DEVICE(vkQueuePresentKHR);
		VK_LOAD_DEVICE(vkQueueWaitIdle);
		VK_LOAD_DEVICE(vkCreateBuffer);
		VK_LOAD_DEVICE(vkDestroyBuffer);
		VK_LOAD_DEVICE(vkGetBufferMemoryRequirements);
		VK_LOAD_DEVICE(vkBindBufferMemory);
		VK_LOAD_DEVICE(vkCreateImage);
		VK_LOAD_DEVICE(vkDestroyImage);
		VK_LOAD_DEVICE(vkGetImageMemoryRequirements);
		VK_LOAD_DEVICE(vkBindImageMemory);
		VK_LOAD_DEVICE(vkAllocateMemory);
		VK_LOAD_DEVICE(vkFreeMemory);
		VK_LOAD_DEVICE(vkMapMemory);
		VK_LOAD_DEVICE(vkUnmapMemory);
		VK_LOAD_DEVICE(vkCreateSampler);
		VK_LOAD_DEVICE(vkDestroySampler);
		VK_LOAD_DEVICE(vkCreateShaderModule);
		VK_LOAD_DEVICE(vkDestroyShaderModule);
		VK_LOAD_DEVICE(vkCreateDescriptorSetLayout);
		VK_LOAD_DEVICE(vkDestroyDescriptorSetLayout);
		VK_LOAD_DEVICE(vkCreateDescriptorPool);
		VK_LOAD_DEVICE(vkDestroyDescriptorPool);
		VK_LOAD_DEVICE(vkResetDescriptorPool);
		VK_LOAD_DEVICE(vkAllocateDescriptorSets);
		VK_LOAD_DEVICE(vkUpdateDescriptorSets);
		VK_LOAD_DEVICE(vkCreatePipelineLayout);
		VK_LOAD_DEVICE(vkDestroyPipelineLayout);
		VK_LOAD_DEVICE(vkCreateGraphicsPipelines);
		VK_LOAD_DEVICE(vkDestroyPipeline);
		VK_LOAD_DEVICE(vkCreatePipelineCache);
		VK_LOAD_DEVICE(vkDestroyPipelineCache);
		VK_LOAD_DEVICE(vkCmdCopyImageToBuffer);
		VK_LOAD_DEVICE(vkCreateRenderPass);
		VK_LOAD_DEVICE(vkDestroyRenderPass);
		VK_LOAD_DEVICE(vkCreateFramebuffer);
		VK_LOAD_DEVICE(vkDestroyFramebuffer);
		VK_LOAD_DEVICE(vkCmdBeginRenderPass);
		VK_LOAD_DEVICE(vkCmdEndRenderPass);
		VK_LOAD_DEVICE(vkCmdBindPipeline);
		VK_LOAD_DEVICE(vkCmdBindVertexBuffers);
		VK_LOAD_DEVICE(vkCmdBindIndexBuffer);
		VK_LOAD_DEVICE(vkCmdBindDescriptorSets);
		VK_LOAD_DEVICE(vkCmdSetViewport);
		VK_LOAD_DEVICE(vkCmdSetScissor);
		VK_LOAD_DEVICE(vkCmdDraw);
		VK_LOAD_DEVICE(vkCmdDrawIndexed);
		VK_LOAD_DEVICE(vkCmdCopyBuffer);
		VK_LOAD_DEVICE(vkCmdCopyBufferToImage);
		VK_LOAD_DEVICE(vkCmdClearAttachments);
		VK_LOAD_DEVICE(vkCmdBlitImage);
#undef VK_LOAD_DEVICE
	}
}
