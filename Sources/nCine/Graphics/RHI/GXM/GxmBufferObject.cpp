#include "GxmBufferObject.h"
#include "GxmDevice.h"

#include "../../../../Main.h"

#include <cstring>

namespace nCine::RHI::GXM
{
	std::uint32_t GxmBufferObject::nextHandle_ = 0;

	GxmBufferObject::GxmBufferObject(BufferTarget target)
		: handle_(++nextHandle_), target_(target), data_(nullptr), size_(0)
	{
	}

	GxmBufferObject::~GxmBufferObject()
	{
		GxmMemory::Free(gpuBlock_);
		data_ = nullptr;
		size_ = 0;
	}

	bool GxmBufferObject::Bind() const
	{
		// There is no per-target binding point in sceGxm: a vertex stream's address is handed to the draw call
		// and an index buffer's to `sceGxmDraw()`, both resolved from the program's bound vertex format
		return true;
	}

	bool GxmBufferObject::Unbind() const
	{
		return true;
	}

	void GxmBufferObject::Reserve(std::size_t size)
	{
		if (size <= size_ && data_ != nullptr) {
			return;
		}

		// A store the GPU reads has to be a mapped memory block. Growing means a fresh block: the pipeline's
		// ring buffers size themselves once at startup, so this is not a per-frame path
		GxmMemory::Free(gpuBlock_);
		const char* name = (target_ == BufferTarget::Index ? "Jazz2:IndexBuffer"
			: (target_ == BufferTarget::Uniform ? "Jazz2:UniformBuffer" : "Jazz2:VertexBuffer"));
		gpuBlock_ = GxmMemory::Alloc(name, std::uint32_t(size), SCE_GXM_MEMORY_ATTRIB_READ);
		if (!gpuBlock_.IsValid()) {
			LOGE("Failed to allocate {} bytes of GPU-visible memory for a buffer object", size);
			data_ = nullptr;
			size_ = 0;
			return;
		}

		data_ = static_cast<std::uint8_t*>(gpuBlock_.Base);
		size_ = size;
	}

	void GxmBufferObject::BufferData(std::size_t size, const void* data, BufferUsage usage)
	{
		static_cast<void>(usage);

		Reserve(size);
		if (data_ == nullptr) {
			return;
		}
		if (data != nullptr) {
			std::memcpy(data_, data, size);
		} else {
			std::memset(data_, 0, size);
		}
	}

	void GxmBufferObject::BufferSubData(std::size_t offset, std::size_t size, const void* data)
	{
		if (data == nullptr || data_ == nullptr || offset + size > size_) {
			return;
		}
		std::memcpy(data_ + offset, data, size);
	}

	void GxmBufferObject::BufferStorage(std::size_t size, const void* data, MapFlags flags)
	{
		static_cast<void>(flags);
		BufferData(size, data, BufferUsage::StaticDraw);
	}

	void GxmBufferObject::BindBufferBase(std::uint32_t index)
	{
		BindBufferRange(index, 0, size_);
	}

	void GxmBufferObject::BindBufferRange(std::uint32_t index, std::size_t offset, std::size_t size)
	{
		if (data_ == nullptr || offset + size > size_) {
			return;
		}
		GxmDevice::BindUniformRange(index, data_ + offset, std::uint32_t(size));
	}

	void* GxmBufferObject::MapBufferRange(std::size_t offset, std::size_t length, MapFlags access)
	{
		static_cast<void>(access);
		if (data_ == nullptr || offset + length > size_) {
			return nullptr;
		}
		return data_ + offset;
	}

	void GxmBufferObject::FlushMappedBufferRange(std::size_t offset, std::size_t length)
	{
		static_cast<void>(offset);
		static_cast<void>(length);
	}

	bool GxmBufferObject::Unmap()
	{
		return true;
	}

	void GxmBufferObject::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
