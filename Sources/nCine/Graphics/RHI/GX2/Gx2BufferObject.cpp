#include "Gx2BufferObject.h"
#include "Gx2Device.h"

#include "../../../../Main.h"

#include <cstring>

#include <gx2/mem.h>

namespace nCine::RHI::GX2
{
	namespace
	{
		/** @brief Alignment of every store: 256 bytes covers uniform blocks, which need the most */
		constexpr std::uint32_t StoreAlignment = 256;
	}

	std::uint32_t Gx2BufferObject::_nextHandle = 0;

	Gx2BufferObject::Gx2BufferObject(BufferTarget target)
		: _handle(++_nextHandle), _target(target), _data(nullptr), _size(0), _mappedOffset(0), _mappedLength(0), _lastUse(0)
	{
	}

	Gx2BufferObject::~Gx2BufferObject()
	{
		Gx2Device::RetireBlock(_gpuBlock);
		_data = nullptr;
		_size = 0;
	}

	bool Gx2BufferObject::Bind() const
	{
		// There is no per-target binding point in GX2: a vertex stream is bound with
		// `GX2SetAttribBuffer()` and an index buffer is passed to `GX2DrawIndexedEx()`, both resolved from
		// the program's bound vertex format at draw time
		return true;
	}

	bool Gx2BufferObject::Unbind() const
	{
		return true;
	}

	bool Gx2BufferObject::Reserve(std::size_t size)
	{
		if (size <= _size && _data != nullptr) {
			return false;
		}

		// Growing means a fresh block: the pipeline's ring buffers size themselves once at startup, so this is
		// not a per-frame path. The old block may still be read by a queued draw, so it is retired, not freed.
		Gx2Device::RetireBlock(_gpuBlock);
		_gpuBlock = Gx2Memory::Alloc(std::uint32_t(size), StoreAlignment);
		if (!_gpuBlock.IsValid()) {
			LOGE("Failed to allocate {} bytes of GPU-visible memory for a buffer object", size);
			_data = nullptr;
			_size = 0;
			return false;
		}

		_data = static_cast<std::uint8_t*>(_gpuBlock.Base);
		_size = size;
		_lastUse = 0;
		std::memset(_data, 0, size);
		FlushRange(0, size);
		return true;
	}

	void Gx2BufferObject::PrepareCpuWrite(bool discardsAll)
	{
		if (_data == nullptr || Gx2Device::IsUseRetired(_lastUse)) {
			return;
		}
		if (discardsAll) {
			// What a driver does for an orphaned buffer: the queued draws keep reading the old block, which is retired
			// and freed once they have run, and the CPU writes into a new one
			Gx2Memory::Block fresh = Gx2Memory::Alloc(std::uint32_t(_size), StoreAlignment);
			if (fresh.IsValid()) {
				Gx2Device::RetireBlock(_gpuBlock);
				_gpuBlock = fresh;
				_data = static_cast<std::uint8_t*>(_gpuBlock.Base);
				_lastUse = 0;
				return;
			}
		}
		Gx2Device::WaitForUse(_lastUse);
		_lastUse = 0;
	}

	void Gx2BufferObject::BufferData(std::size_t size, const void* data, BufferUsage usage)
	{
		static_cast<void>(usage);

		if (!Reserve(size)) {
			// The same block again: the previous contents may go, which is cheap only if no queued draw reads them
			PrepareCpuWrite(true);
		}
		if (_data == nullptr || data == nullptr) {
			// Orphaning (`Geometry::CommitVertices()` on a custom buffer without mapping): the contents are undefined
			// now and nothing is written. Do not zero them: in milestone 5, when RenderBuffersManager still orphaned
			// its streaming buffers this way, that hit vertices queued draws had not read yet.
			return;
		}
		std::memcpy(_data, data, size);
		FlushRange(0, size);
	}

	void Gx2BufferObject::BufferSubData(std::size_t offset, std::size_t size, const void* data)
	{
		if (data == nullptr || _data == nullptr || offset + size > _size) {
			return;
		}
		PrepareCpuWrite(offset == 0 && size == _size);
		std::memcpy(_data + offset, data, size);
		FlushRange(offset, size);
	}

	void Gx2BufferObject::BufferStorage(std::size_t size, const void* data, MapFlags flags)
	{
		static_cast<void>(flags);
		BufferData(size, data, BufferUsage::StaticDraw);
	}

	void Gx2BufferObject::BindBufferBase(std::uint32_t index)
	{
		BindBufferRange(index, 0, _size);
	}

	void Gx2BufferObject::BindBufferRange(std::uint32_t index, std::size_t offset, std::size_t size)
	{
		if (_data == nullptr || offset + size > _size) {
			return;
		}
		Gx2Device::BindUniformRange(index, _data + offset, std::uint32_t(size));
	}

	void* Gx2BufferObject::MapBufferRange(std::size_t offset, std::size_t length, MapFlags access)
	{
		if (_data == nullptr || offset + length > _size) {
			return nullptr;
		}
		if ((access & MapFlags::Persistent) != MapFlags::Persistent) {
			// A persistent mapping is the section ring of RenderBuffersManager, which fences its own sections
			const bool discardsAll = ((access & MapFlags::InvalidateBuffer) == MapFlags::InvalidateBuffer) ||
				(offset == 0 && length == _size && (access & MapFlags::InvalidateRange) == MapFlags::InvalidateRange);
			PrepareCpuWrite(discardsAll);
		}
		_mappedOffset = offset;
		_mappedLength = length;
		return _data + offset;
	}

	void Gx2BufferObject::FlushMappedBufferRange(std::size_t offset, std::size_t length)
	{
		// The offset is relative to the mapped range, as in glFlushMappedBufferRange()
		FlushRange(_mappedOffset + offset, length);
	}

	bool Gx2BufferObject::Unmap()
	{
		FlushRange(_mappedOffset, _mappedLength);
		_mappedOffset = 0;
		_mappedLength = 0;
		return true;
	}

	void Gx2BufferObject::FlushRange(std::size_t offset, std::size_t length)
	{
		if (_data == nullptr || length == 0 || offset >= _size) {
			return;
		}
		if (offset + length > _size) {
			length = _size - offset;
		}
		if (_target == BufferTarget::Uniform) {
			// The GPU never reads a uniform buffer: the device copies the bound ranges into its uniform ring,
			// byte-swapped, at draw time, so there is nothing to push out of the CPU cache
			return;
		}
		Gx2Memory::FlushCpuWrites(_data + offset, std::uint32_t(length), GX2_INVALIDATE_MODE_ATTRIBUTE_BUFFER);
	}

	void Gx2BufferObject::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
