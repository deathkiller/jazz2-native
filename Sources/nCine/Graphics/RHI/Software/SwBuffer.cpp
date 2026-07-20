#include "SwBuffer.h"
#include "SwDevice.h"

#include <cstring>

namespace nCine::RHI::Software
{
	std::uint32_t SwBuffer::nextHandle_ = 1;

	SwBuffer::SwBuffer(BufferTarget target)
		: handle_(nextHandle_++), target_(target)
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
		storage_.assign(size, std::uint8_t(0));
		if (data != nullptr && size > 0) {
			std::memcpy(storage_.data(), data, size);
		}
	}

	void SwBuffer::BufferSubData(std::size_t offset, std::size_t size, const void* data)
	{
		if (data == nullptr || size == 0 || offset + size > storage_.size()) {
			return;
		}
		std::memcpy(storage_.data() + offset, data, size);
	}

	void SwBuffer::BufferStorage(std::size_t size, const void* data, MapFlags flags)
	{
		// The software backend keeps everything in a resizable host store, so "immutable storage" is just
		// a plain (re)allocation; the storage/mapping flags do not apply
		static_cast<void>(flags);
		storage_.assign(size, std::uint8_t(0));
		if (data != nullptr && size > 0) {
			std::memcpy(storage_.data(), data, size);
		}
	}

	void SwBuffer::BindBufferBase(std::uint32_t index)
	{
		BindBufferRange(index, 0, storage_.size());
	}

	void SwBuffer::BindBufferRange(std::uint32_t index, std::size_t offset, std::size_t size)
	{
		if (offset > storage_.size()) {
			return;
		}
		if (offset + size > storage_.size()) {
			size = storage_.size() - offset;
		}
		SwDevice::BindUniformRange(index, storage_.data() + offset, std::uint32_t(size));
	}

	void* SwBuffer::MapBufferRange(std::size_t offset, std::size_t length, MapFlags access)
	{
		static_cast<void>(length);
		static_cast<void>(access);
		if (offset > storage_.size()) {
			return nullptr;
		}
		return storage_.data() + offset;
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
