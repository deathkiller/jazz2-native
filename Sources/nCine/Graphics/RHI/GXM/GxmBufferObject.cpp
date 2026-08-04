#include "GxmBufferObject.h"
#include "GxmDevice.h"

#include "../../../../Main.h"

#include <cstring>

namespace nCine::RHI::GXM
{
	std::uint32_t GxmBufferObject::_nextHandle = 0;

	GxmBufferObject::GxmBufferObject(BufferTarget target)
		: _handle(++_nextHandle), _target(target), _data(nullptr), _size(0)
	{
	}

	GxmBufferObject::~GxmBufferObject()
	{
		GxmMemory::Free(_gpuBlock);
		_data = nullptr;
		_size = 0;
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
		if (size <= _size && _data != nullptr) {
			return;
		}

		// A store the GPU reads has to be a mapped memory block. Growing means a fresh block: the pipeline's
		// ring buffers size themselves once at startup, so this is not a per-frame path
		GxmMemory::Free(_gpuBlock);
		const char* name = (_target == BufferTarget::Index ? "Jazz2:IndexBuffer"
			: (_target == BufferTarget::Uniform ? "Jazz2:UniformBuffer" : "Jazz2:VertexBuffer"));
		_gpuBlock = GxmMemory::Alloc(name, std::uint32_t(size), SCE_GXM_MEMORY_ATTRIB_READ);
		if (!_gpuBlock.IsValid()) {
			LOGE("Failed to allocate {} bytes of GPU-visible memory for a buffer object", size);
			_data = nullptr;
			_size = 0;
			return;
		}

		_data = static_cast<std::uint8_t*>(_gpuBlock.Base);
		_size = size;
	}

	void GxmBufferObject::BufferData(std::size_t size, const void* data, BufferUsage usage)
	{
		static_cast<void>(usage);

		Reserve(size);
		if (_data == nullptr) {
			return;
		}
		if (data != nullptr) {
			std::memcpy(_data, data, size);
		} else {
			std::memset(_data, 0, size);
		}
	}

	void GxmBufferObject::BufferSubData(std::size_t offset, std::size_t size, const void* data)
	{
		if (data == nullptr || _data == nullptr || offset + size > _size) {
			return;
		}
		std::memcpy(_data + offset, data, size);
	}

	void GxmBufferObject::BufferStorage(std::size_t size, const void* data, MapFlags flags)
	{
		static_cast<void>(flags);
		BufferData(size, data, BufferUsage::StaticDraw);
	}

	void GxmBufferObject::BindBufferBase(std::uint32_t index)
	{
		BindBufferRange(index, 0, _size);
	}

	void GxmBufferObject::BindBufferRange(std::uint32_t index, std::size_t offset, std::size_t size)
	{
		if (_data == nullptr || offset + size > _size) {
			return;
		}
		GxmDevice::BindUniformRange(index, _data + offset, std::uint32_t(size));
	}

	void* GxmBufferObject::MapBufferRange(std::size_t offset, std::size_t length, MapFlags access)
	{
		static_cast<void>(access);
		if (_data == nullptr || offset + length > _size) {
			return nullptr;
		}
		return _data + offset;
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
