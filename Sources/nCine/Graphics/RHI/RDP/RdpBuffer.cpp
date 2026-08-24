#include "RdpBuffer.h"
#include "RdpDevice.h"

#include <cstring>

namespace nCine::RHI::RDP
{
	std::uint32_t RdpBuffer::_nextHandle = 1;

	RdpBuffer::RdpBuffer(BufferTarget target)
		: _handle(_nextHandle++), _target(target)
	{
	}

	bool RdpBuffer::Bind() const
	{
		// No bound-buffer cache; the draw dispatch reads from the ranges forwarded by BindBufferRange(),
		// so this only reports that a bind "was issued"
		return true;
	}

	bool RdpBuffer::Unbind() const
	{
		return true;
	}

	void RdpBuffer::BufferData(std::size_t size, const void* data, BufferUsage usage)
	{
		static_cast<void>(usage);
		if (data == nullptr && size == _storage.size()) {
			// Same-size orphaning (BufferData with no data is how the streaming buffers discard last
			// frame's contents) - the contract leaves the contents undefined, so re-zeroing the whole
			// store every frame is nothing but wasted memory traffic
			return;
		}
		// Supplied data replaces the store outright; only a plain allocation is zero-filled (writing
		// every byte twice - a zero fill and then the copy - is pure memory traffic on this bus)
		if (data != nullptr && size > 0) {
			const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
			_storage.assign(bytes, bytes + size);
		} else {
			_storage.assign(size, std::uint8_t(0));
		}
	}

	void RdpBuffer::BufferSubData(std::size_t offset, std::size_t size, const void* data)
	{
		// Two comparisons rather than one sum: offset + size can wrap around std::size_t (32-bit on
		// this ABI), and a wrapped sum would pass the check while the memcpy runs far past the store
		if (data == nullptr || size == 0 || offset > _storage.size() || size > _storage.size() - offset) {
			return;
		}
		std::memcpy(_storage.data() + offset, data, size);
	}

	void RdpBuffer::BufferStorage(std::size_t size, const void* data, MapFlags flags)
	{
		// Everything lives in a resizable host store, so "immutable storage" is just a plain
		// (re)allocation; the storage/mapping flags do not apply
		static_cast<void>(flags);
		if (data != nullptr && size > 0) {
			const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
			_storage.assign(bytes, bytes + size);
		} else {
			_storage.assign(size, std::uint8_t(0));
		}
	}

	void RdpBuffer::BindBufferBase(std::uint32_t index)
	{
		BindBufferRange(index, 0, _storage.size());
	}

	void RdpBuffer::BindBufferRange(std::uint32_t index, std::size_t offset, std::size_t size)
	{
		if (offset > _storage.size()) {
			return;
		}
		// Clamped without the offset + size sum, which can wrap around std::size_t (32-bit here)
		if (size > _storage.size() - offset) {
			size = _storage.size() - offset;
		}
		RdpDevice::BindUniformRange(index, _storage.data() + offset, std::uint32_t(size));
	}

	void* RdpBuffer::MapBufferRange(std::size_t offset, std::size_t length, MapFlags access)
	{
		static_cast<void>(length);
		static_cast<void>(access);
		if (offset > _storage.size()) {
			return nullptr;
		}
		return _storage.data() + offset;
	}

	void RdpBuffer::FlushMappedBufferRange(std::size_t offset, std::size_t length)
	{
		static_cast<void>(offset);
		static_cast<void>(length);
	}

	bool RdpBuffer::Unmap()
	{
		return true;
	}

	void RdpBuffer::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
