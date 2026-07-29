#include "PvrBuffer.h"
#include "PvrDevice.h"

#include <cstring>

namespace nCine::RHI::PVR
{
	std::uint32_t PvrBuffer::nextHandle_ = 1;

	PvrBuffer::PvrBuffer(BufferTarget target)
		: handle_(nextHandle_++), target_(target)
	{
	}

	bool PvrBuffer::Bind() const
	{
		// No bound-buffer cache; the draw dispatch reads from the ranges forwarded by BindBufferRange(),
		// so this only reports that a bind "was issued"
		return true;
	}

	bool PvrBuffer::Unbind() const
	{
		return true;
	}

	void PvrBuffer::BufferData(std::size_t size, const void* data, BufferUsage usage)
	{
		static_cast<void>(usage);
		storage_.assign(size, std::uint8_t(0));
		if (data != nullptr && size > 0) {
			std::memcpy(storage_.data(), data, size);
		}
	}

	void PvrBuffer::BufferSubData(std::size_t offset, std::size_t size, const void* data)
	{
		if (data == nullptr || size == 0 || offset + size > storage_.size()) {
			return;
		}
		std::memcpy(storage_.data() + offset, data, size);
	}

	void PvrBuffer::BufferStorage(std::size_t size, const void* data, MapFlags flags)
	{
		// Everything lives in a resizable host store, so "immutable storage" is just a plain
		// (re)allocation; the storage/mapping flags do not apply
		static_cast<void>(flags);
		storage_.assign(size, std::uint8_t(0));
		if (data != nullptr && size > 0) {
			std::memcpy(storage_.data(), data, size);
		}
	}

	void PvrBuffer::BindBufferBase(std::uint32_t index)
	{
		BindBufferRange(index, 0, storage_.size());
	}

	void PvrBuffer::BindBufferRange(std::uint32_t index, std::size_t offset, std::size_t size)
	{
		if (offset > storage_.size()) {
			return;
		}
		if (offset + size > storage_.size()) {
			size = storage_.size() - offset;
		}
		PvrDevice::BindUniformRange(index, storage_.data() + offset, std::uint32_t(size));
	}

	void* PvrBuffer::MapBufferRange(std::size_t offset, std::size_t length, MapFlags access)
	{
		static_cast<void>(length);
		static_cast<void>(access);
		if (offset > storage_.size()) {
			return nullptr;
		}
		return storage_.data() + offset;
	}

	void PvrBuffer::FlushMappedBufferRange(std::size_t offset, std::size_t length)
	{
		static_cast<void>(offset);
		static_cast<void>(length);
	}

	bool PvrBuffer::Unmap()
	{
		return true;
	}

	void PvrBuffer::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
