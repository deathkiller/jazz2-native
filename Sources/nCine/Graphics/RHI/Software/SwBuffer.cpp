#include "SwBuffer.h"
#include "SwDevice.h"

#include <cstring>

namespace nCine::RHI::Software
{
	std::uint32_t SwBuffer::_nextHandle = 1;

	SwBuffer::SwBuffer(BufferTarget target)
		: _handle(_nextHandle++), _target(target)
	{
	}

	bool SwBuffer::Bind() const
	{
		// The software backend does not consult a bound-buffer cache; the effects read from the ranges
		// forwarded by BindBufferRange(), so this only reports that a bind "was issued"
		return true;
	}

	bool SwBuffer::Unbind() const
	{
		return true;
	}

	void SwBuffer::BufferData(std::size_t size, const void* data, BufferUsage usage)
	{
		static_cast<void>(usage);
		_storage.assign(size, std::uint8_t(0));
		if (data != nullptr && size > 0) {
			std::memcpy(_storage.data(), data, size);
		}
	}

	void SwBuffer::BufferSubData(std::size_t offset, std::size_t size, const void* data)
	{
		if (data == nullptr || size == 0 || offset + size > _storage.size()) {
			return;
		}
		std::memcpy(_storage.data() + offset, data, size);
	}

	void SwBuffer::BufferStorage(std::size_t size, const void* data, MapFlags flags)
	{
		// The software backend keeps everything in a resizable host store, so "immutable storage" is just
		// a plain (re)allocation; the storage/mapping flags do not apply
		static_cast<void>(flags);
		_storage.assign(size, std::uint8_t(0));
		if (data != nullptr && size > 0) {
			std::memcpy(_storage.data(), data, size);
		}
	}

	void SwBuffer::BindBufferBase(std::uint32_t index)
	{
		BindBufferRange(index, 0, _storage.size());
	}

	void SwBuffer::BindBufferRange(std::uint32_t index, std::size_t offset, std::size_t size)
	{
		if (offset > _storage.size()) {
			return;
		}
		if (offset + size > _storage.size()) {
			size = _storage.size() - offset;
		}
		SwDevice::BindUniformRange(index, _storage.data() + offset, std::uint32_t(size));
	}

	void* SwBuffer::MapBufferRange(std::size_t offset, std::size_t length, MapFlags access)
	{
		static_cast<void>(length);
		static_cast<void>(access);
		if (offset > _storage.size()) {
			return nullptr;
		}
		return _storage.data() + offset;
	}

	void SwBuffer::FlushMappedBufferRange(std::size_t offset, std::size_t length)
	{
		static_cast<void>(offset);
		static_cast<void>(length);
	}

	bool SwBuffer::Unmap()
	{
		return true;
	}

	void SwBuffer::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
