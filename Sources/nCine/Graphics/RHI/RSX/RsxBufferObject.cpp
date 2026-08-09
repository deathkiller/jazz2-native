#include "RsxBufferObject.h"
#include "RsxDevice.h"

#include "../../../../Main.h"

#include <cstring>

namespace nCine::RHI::RSX
{
	namespace
	{
		/**
			@brief Alignment every buffer store is allocated with

			128 rather than the 64 the hardware demands of an offset it fetches from: an index array and a
			vertex stream are both read through the GPU's 128-byte memory bursts, so starting a store on that
			boundary means the first burst of a draw carries only data the draw asked for.
		*/
		constexpr std::uint32_t StoreAlignment = 128;
	}

	std::uint32_t RsxBufferObject::_nextHandle = 0;

	RsxBufferObject::RsxBufferObject(BufferTarget target)
		: _handle(++_nextHandle), _target(target), _data(nullptr), _size(0)
	{
	}

	RsxBufferObject::~RsxBufferObject()
	{
		RsxVram::Free(_gpuBlock);
		_data = nullptr;
		_size = 0;
	}

	bool RsxBufferObject::Bind() const
	{
		// There is no per-target binding point on the RSX: a vertex stream's offset is handed to
		// `rsxBindVertexArrayAttrib()` and an index buffer's to `rsxDrawIndexArray()`, both resolved from
		// the program's bound vertex format at draw time
		return true;
	}

	bool RsxBufferObject::Unbind() const
	{
		return true;
	}

	void RsxBufferObject::Reserve(std::size_t size)
	{
		if (size <= _size && _data != nullptr) {
			return;
		}

		// A store the GPU reads has to live in memory it can address by offset. Growing means a fresh block:
		// the pipeline's ring buffers size themselves once at startup, so this is not a per-frame path.
		// Main memory rather than local: the PPE rewrites these every frame (see the header).
		RsxVram::Free(_gpuBlock);
		_gpuBlock = RsxVram::Alloc(std::uint32_t(size), StoreAlignment, RsxVram::Location::Main);
		if (!_gpuBlock.IsValid()) {
			LOGE("Failed to allocate {} bytes of GPU-visible memory for a buffer object", size);
			_data = nullptr;
			_size = 0;
			return;
		}

		_data = static_cast<std::uint8_t*>(_gpuBlock.Base);
		_size = size;
	}

	void RsxBufferObject::BufferData(std::size_t size, const void* data, BufferUsage usage)
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

	void RsxBufferObject::BufferSubData(std::size_t offset, std::size_t size, const void* data)
	{
		if (data == nullptr || _data == nullptr || offset + size > _size) {
			return;
		}
		std::memcpy(_data + offset, data, size);
	}

	void RsxBufferObject::BufferStorage(std::size_t size, const void* data, MapFlags flags)
	{
		static_cast<void>(flags);
		BufferData(size, data, BufferUsage::StaticDraw);
	}

	void RsxBufferObject::BindBufferBase(std::uint32_t index)
	{
		BindBufferRange(index, 0, _size);
	}

	void RsxBufferObject::BindBufferRange(std::uint32_t index, std::size_t offset, std::size_t size)
	{
		if (_data == nullptr || offset + size > _size) {
			return;
		}
		RsxDevice::BindUniformRange(index, _data + offset, std::uint32_t(size));
	}

	void* RsxBufferObject::MapBufferRange(std::size_t offset, std::size_t length, MapFlags access)
	{
		static_cast<void>(access);
		if (_data == nullptr || offset + length > _size) {
			return nullptr;
		}
		return _data + offset;
	}

	void RsxBufferObject::FlushMappedBufferRange(std::size_t offset, std::size_t length)
	{
		static_cast<void>(offset);
		static_cast<void>(length);
	}

	bool RsxBufferObject::Unmap()
	{
		return true;
	}

	void RsxBufferObject::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}
}
