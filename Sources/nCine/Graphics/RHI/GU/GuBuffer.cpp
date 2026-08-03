#include "GuBuffer.h"
#include "GuDevice.h"

#include <cstring>

namespace nCine::RHI::GU
{
	std::uint32_t GuBuffer::nextHandle_ = 1;

	GuBuffer::GuBuffer(BufferTarget target)
		: handle_(nextHandle_++), target_(target)
	{
	}

	bool GuBuffer::Bind() const
	{
		// No bound-buffer cache; the draw dispatch reads from the ranges forwarded by BindBufferRange(),
		// so this only reports that a bind "was issued"
		return true;
	}

	bool GuBuffer::Unbind() const
	{
		return true;
	}

	void GuBuffer::BufferData(std::size_t size, const void* data, BufferUsage usage)
	{
		static_cast<void>(usage);
		if (data == nullptr && size == storage_.size()) {
			// Same-size orphaning (BufferData with no data is how the streaming buffers discard last
			// frame's contents) - the contract leaves the contents undefined, so re-zeroing the whole
			// store every frame is nothing but wasted memory traffic
			return;
		}
		storage_.assign(size, std::uint8_t(0));
		if (data != nullptr && size > 0) {
			std::memcpy(storage_.data(), data, size);
		}
	}

	void GuBuffer::BufferSubData(std::size_t offset, std::size_t size, const void* data)
	{
		if (data == nullptr || size == 0 || offset + size > storage_.size()) {
			return;
		}
		std::memcpy(storage_.data() + offset, data, size);
	}

	void GuBuffer::BufferStorage(std::size_t size, const void* data, MapFlags flags)
	{
		// Everything lives in a resizable host store, so "immutable storage" is just a plain
		// (re)allocation; the storage/mapping flags do not apply
		static_cast<void>(flags);
		storage_.assign(size, std::uint8_t(0));
		if (data != nullptr && size > 0) {
			std::memcpy(storage_.data(), data, size);
		}
	}

	void GuBuffer::BindBufferBase(std::uint32_t index)
	{
		BindBufferRange(index, 0, storage_.size());
	}

	void GuBuffer::BindBufferRange(std::uint32_t index, std::size_t offset, std::size_t size)
	{
		if (offset > storage_.size()) {
			return;
		}
		if (offset + size > storage_.size()) {
			size = storage_.size() - offset;
		}
		GuDevice::BindUniformRange(index, storage_.data() + offset, std::uint32_t(size));
	}

	void* GuBuffer::MapBufferRange(std::size_t offset, std::size_t length, MapFlags access)
	{
		static_cast<void>(length);
		static_cast<void>(access);
		if (offset > storage_.size()) {
			return nullptr;
		}
		return storage_.data() + offset;
	}

	void GuBuffer::FlushMappedBufferRange(std::size_t offset, std::size_t length)
	{
		static_cast<void>(offset);
		static_cast<void>(length);
	}

	bool GuBuffer::Unmap()
	{
		return true;
	}

	void GuBuffer::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
