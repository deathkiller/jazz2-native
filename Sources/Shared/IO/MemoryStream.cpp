#include "MemoryStream.h"
#include "../Containers/GrowableArray.h"

#include <cstring>

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	MemoryStream::MemoryStream(std::int32_t initialCapacity)
		: _seekOffset(0), _mode(AccessMode::Growable)
	{
		_type = Type::Memory;
		_size = 0;

		if (initialCapacity > 0) {
			Containers::arrayReserve(_buffer, initialCapacity);
		}
	}

	MemoryStream::MemoryStream(std::uint8_t* bufferPtr, std::int32_t bufferSize)
		: _buffer(bufferPtr, bufferSize, [](std::uint8_t* data, std::size_t size) { }), _seekOffset(0), _mode(AccessMode::Writable)
	{
		_type = Type::Memory;
		_size = bufferSize;
	}

	MemoryStream::MemoryStream(const std::uint8_t* bufferPtr, std::int32_t bufferSize)
		: _buffer(const_cast<std::uint8_t*>(bufferPtr), bufferSize, [](std::uint8_t* data, std::size_t size) { }), _seekOffset(0), _mode(AccessMode::ReadOnly)
	{
		_type = Type::Memory;
		_size = bufferSize;
	}

	void MemoryStream::Close()
	{
		_size = 0;
		_seekOffset = 0;
		_mode = AccessMode::None;
	}

	std::int32_t MemoryStream::Seek(std::int32_t offset, SeekOrigin origin)
	{
		std::int32_t seekValue;
		switch (origin) {
			case SeekOrigin::Begin:
				seekValue = offset;
				break;
			case SeekOrigin::Current:
				seekValue = _seekOffset + offset;
				break;
			case SeekOrigin::End:
				seekValue = _size + offset;
				break;
			default:
				seekValue = -1;
				break;
		}

		if (seekValue < 0 || seekValue > _size) {
			seekValue = -1;
		} else {
			_seekOffset = seekValue;
		}
		return seekValue;
	}

	std::int32_t MemoryStream::GetPosition() const
	{
		return _seekOffset;
	}

	std::int32_t MemoryStream::Read(void* buffer, std::int32_t bytes)
	{
		DEATH_ASSERT(buffer != nullptr, 0, "buffer is nullptr");

		std::int32_t bytesRead = 0;

		if (_mode != AccessMode::None) {
			bytesRead = (_seekOffset + bytes > _size ? (_size - _seekOffset) : bytes);
			std::memcpy(buffer, _buffer.data() + _seekOffset, bytesRead);
			_seekOffset += bytesRead;
		}

		return bytesRead;
	}

	std::int32_t MemoryStream::Write(const void* buffer, std::int32_t bytes)
	{
		DEATH_ASSERT(buffer != nullptr, 0, "buffer is nullptr");

		std::int32_t bytesWritten = 0;

		if (_mode == AccessMode::Writable || _mode == AccessMode::Growable) {
			if (_mode == AccessMode::Growable && _size < _seekOffset + bytes) {
				_size = _seekOffset + bytes;
				Containers::arrayResize(_buffer, Containers::NoInit, _size);
			}

			bytesWritten = (_seekOffset + bytes > _size ? (_size - _seekOffset) : bytes);
			std::memcpy(_buffer.data() + _seekOffset, buffer, bytesWritten);
			_seekOffset += bytesWritten;
		}

		return bytesWritten;
	}

	bool MemoryStream::IsValid() const
	{
		return true;
	}

	void MemoryStream::ReserveCapacity(std::int32_t bytes)
	{
		if (_mode == AccessMode::Growable) {
			Containers::arrayReserve(_buffer, _seekOffset + bytes);
		}
	}

	std::int32_t MemoryStream::FetchFromStream(Stream& s, std::int32_t bytes)
	{
		std::int32_t bytesFetched = 0;

		if (bytes > 0 && (_mode == AccessMode::Writable || _mode == AccessMode::Growable)) {
			if (_size < _seekOffset + bytes) {
				_size = _seekOffset + bytes;
				Containers::arrayResize(_buffer, Containers::NoInit, _size);
			}

			std::int32_t bytesToRead = (_seekOffset + bytes > _size ? (_size - _seekOffset) : bytes);
			while (bytesToRead > 0) {
				std::int32_t bytesRead = s.Read(_buffer.data() + _seekOffset, bytesToRead);
				bytesFetched += bytesRead;
				bytesToRead -= bytesRead;
			}
			_seekOffset += bytesFetched;
		}

		return bytesFetched;
	}
}}
