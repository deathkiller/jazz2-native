#include "VulkanBufferObject.h"
#include "VulkanDevice.h"
#include "VulkanCommon.h"

#include <cstring>

namespace nCine::RHI::Vulkan
{
	std::uint32_t VulkanBufferObject::nextHandle_ = 1;

	VulkanBufferObject::VulkanBufferObject(BufferTarget target)
		: handle_(nextHandle_++), target_(target),
			gpuBuffer_(0), gpuMemory_(0), gpuMapped_(nullptr), gpuCapacity_(0), gpuDirty_(true)
	{
	}

	VulkanBufferObject::~VulkanBufferObject()
	{
		ReleaseGpu();
	}

	void VulkanBufferObject::ReleaseGpu() const
	{
		// Unmapping is a CPU-side operation (it does not disturb an in-flight GPU read of the buffer), so do it now.
		VkDevice device = VkDeviceHandle();
		if (device != VK_NULL_HANDLE && gpuMapped_ != nullptr) {
			vkUnmapMemory(device, reinterpret_cast<VkDeviceMemory>(gpuMemory_));
		}
		gpuMapped_ = nullptr;
		// Defer the buffer / memory frees: an in-flight frame's command buffer may still bind this buffer as a
		// vertex/index source (up to MaxFramesInFlight frames run concurrently), and this also runs on a mid-frame
		// grow-realloc; freeing now would be a GPU use-after-free. The device frees them once every referencing
		// frame has completed. VkEnqueueDestroy no-ops if the device is already gone (teardown).
		VkEnqueueDestroy(VkDeferredResource::Buffer, gpuBuffer_);
		VkEnqueueDestroy(VkDeferredResource::DeviceMemory, gpuMemory_);
		gpuBuffer_ = 0;
		gpuMemory_ = 0;
		gpuCapacity_ = 0;
	}

	std::uint64_t VulkanBufferObject::GetVkBuffer() const
	{
		// Only the geometry (Vertex/Index) targets get a real VkBuffer; uniform data is routed through the
		// device's per-frame device-aligned uniform ring instead (see VulkanDevice draw path).
		if (target_ == BufferTarget::Uniform) {
			return 0;
		}

		VkDevice device = VkDeviceHandle();
		if (device == VK_NULL_HANDLE || storage_.empty()) {
			return 0;
		}

		// (Re)create when missing or too small (grow-only). Safe mid-frame: the device waits on the previous
		// frame's fence before any draw records, so the old buffer is no longer read by the GPU.
		if (gpuBuffer_ == 0 || gpuCapacity_ < storage_.size()) {
			ReleaseGpu();

			VkBufferCreateInfo bci = {};
			bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bci.size = storage_.size();
			bci.usage = (target_ == BufferTarget::Index) ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT : VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
			bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			VkBuffer buffer = VK_NULL_HANDLE;
			if (vkCreateBuffer(device, &bci, nullptr, &buffer) != VK_SUCCESS) {
				return 0;
			}

			VkMemoryRequirements req;
			vkGetBufferMemoryRequirements(device, buffer, &req);
			const std::uint32_t memType = VkFindMemoryType(req.memoryTypeBits,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			VkMemoryAllocateInfo mai = {};
			mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			mai.allocationSize = req.size;
			mai.memoryTypeIndex = memType;
			VkDeviceMemory memory = VK_NULL_HANDLE;
			if (memType == UINT32_MAX || vkAllocateMemory(device, &mai, nullptr, &memory) != VK_SUCCESS) {
				vkDestroyBuffer(device, buffer, nullptr);
				return 0;
			}
			vkBindBufferMemory(device, buffer, memory, 0);
			void* mapped = nullptr;
			vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapped);

			gpuBuffer_ = reinterpret_cast<std::uint64_t>(buffer);
			gpuMemory_ = reinterpret_cast<std::uint64_t>(memory);
			gpuMapped_ = mapped;
			gpuCapacity_ = storage_.size();
			gpuDirty_ = true;
		}

		if (gpuDirty_ && gpuMapped_ != nullptr) {
			std::memcpy(gpuMapped_, storage_.data(), storage_.size());
			gpuDirty_ = false;
		}
		return gpuBuffer_;
	}

	bool VulkanBufferObject::Bind() const
	{
		return true;
	}

	bool VulkanBufferObject::Unbind() const
	{
		return true;
	}

	void VulkanBufferObject::BufferData(std::size_t size, const void* data, BufferUsage usage)
	{
		static_cast<void>(usage);
		storage_.assign(size, std::uint8_t(0));
		if (data != nullptr && size > 0) {
			std::memcpy(storage_.data(), data, size);
		}
		gpuDirty_ = true;
	}

	void VulkanBufferObject::BufferSubData(std::size_t offset, std::size_t size, const void* data)
	{
		if (data == nullptr || size == 0 || offset + size > storage_.size()) {
			return;
		}
		std::memcpy(storage_.data() + offset, data, size);
		gpuDirty_ = true;
	}

	void VulkanBufferObject::BufferStorage(std::size_t size, const void* data, MapFlags flags)
	{
		// The host store is a plain resizable buffer, so "immutable storage" is just a (re)allocation
		static_cast<void>(flags);
		storage_.assign(size, std::uint8_t(0));
		if (data != nullptr && size > 0) {
			std::memcpy(storage_.data(), data, size);
		}
		gpuDirty_ = true;
	}

	void VulkanBufferObject::BindBufferBase(std::uint32_t index)
	{
		BindBufferRange(index, 0, storage_.size());
	}

	void VulkanBufferObject::BindBufferRange(std::uint32_t index, std::size_t offset, std::size_t size)
	{
		if (offset > storage_.size()) {
			return;
		}
		if (offset + size > storage_.size()) {
			size = storage_.size() - offset;
		}
		VulkanDevice::BindUniformRange(index, storage_.data() + offset, std::uint32_t(size));
	}

	void* VulkanBufferObject::MapBufferRange(std::size_t offset, std::size_t length, MapFlags access)
	{
		static_cast<void>(length);
		static_cast<void>(access);
		if (offset > storage_.size()) {
			return nullptr;
		}
		return storage_.data() + offset;
	}

	void VulkanBufferObject::FlushMappedBufferRange(std::size_t offset, std::size_t length)
	{
		static_cast<void>(offset);
		static_cast<void>(length);
		gpuDirty_ = true;
	}

	bool VulkanBufferObject::Unmap()
	{
		gpuDirty_ = true;
		return true;
	}

	void VulkanBufferObject::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
