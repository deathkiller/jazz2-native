#include "LegacyGlBuffer.h"
#include "LegacyGlDevice.h"

#include <cstring>

namespace nCine::RHI::LegacyGL
{
	std::uint32_t LegacyGlBuffer::_nextHandle = 1;

	LegacyGlBuffer::LegacyGlBuffer(BufferTarget target)
		: _handle(_nextHandle++), _target(target)
	{
	}

	bool LegacyGlBuffer::Bind() const
	{
		// No bound-buffer cache; the draw dispatch reads from the ranges forwarded by BindBufferRange(),
		// so this only reports that a bind "was issued"
		return true;
	}

	bool LegacyGlBuffer::Unbind() const
	{
		return true;
	}

	void LegacyGlBuffer::BufferData(std::size_t size, const void* data, BufferUsage usage)
	{
		static_cast<void>(usage);
		if (data == nullptr && size == _storage.size()) {
			// Same-size orphaning (BufferData with no data is how the streaming buffers discard last
			// frame's contents) - the contract leaves the contents undefined, so re-zeroing the whole
			// store every frame is nothing but wasted memory traffic
			return;
		}
		_storage.assign(size, std::uint8_t(0));
		if (data != nullptr && size > 0) {
			std::memcpy(_storage.data(), data, size);
		}
	}

	void LegacyGlBuffer::BufferSubData(std::size_t offset, std::size_t size, const void* data)
	{
		if (data == nullptr || size == 0 || offset + size > _storage.size()) {
			return;
		}
		std::memcpy(_storage.data() + offset, data, size);
	}

	void LegacyGlBuffer::BufferStorage(std::size_t size, const void* data, MapFlags flags)
	{
		// Everything lives in a resizable host store, so "immutable storage" is just a plain
		// (re)allocation; the storage/mapping flags do not apply
		static_cast<void>(flags);
		_storage.assign(size, std::uint8_t(0));
		if (data != nullptr && size > 0) {
			std::memcpy(_storage.data(), data, size);
		}
	}

	void LegacyGlBuffer::BindBufferBase(std::uint32_t index)
	{
		BindBufferRange(index, 0, _storage.size());
	}

	void LegacyGlBuffer::BindBufferRange(std::uint32_t index, std::size_t offset, std::size_t size)
	{
		if (offset > _storage.size()) {
			return;
		}
		if (offset + size > _storage.size()) {
			size = _storage.size() - offset;
		}
		LegacyGlDevice::BindUniformRange(index, _storage.data() + offset, std::uint32_t(size));
	}

	void* LegacyGlBuffer::MapBufferRange(std::size_t offset, std::size_t length, MapFlags access)
	{
		static_cast<void>(length);
		static_cast<void>(access);
		if (offset > _storage.size()) {
			return nullptr;
		}
		return _storage.data() + offset;
	}

	void LegacyGlBuffer::FlushMappedBufferRange(std::size_t offset, std::size_t length)
	{
		static_cast<void>(offset);
		static_cast<void>(length);
	}

	bool LegacyGlBuffer::Unmap()
	{
		return true;
	}

	void LegacyGlBuffer::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
