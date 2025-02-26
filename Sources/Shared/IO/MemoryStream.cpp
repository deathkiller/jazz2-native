#include "MemoryStream.h"
#include "../Containers/GrowableArray.h"

#include <cstring>

using namespace Death::Containers;

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	MemoryStream::MemoryStream(std::int64_t initialCapacity)
		: _pos(0), _mode(AccessMode::Growable)
	{
		_size = 0;

		if (initialCapacity > 0) {
			arrayReserve(_data, initialCapacity);
		}
	}

	MemoryStream::MemoryStream(void* bufferPtr, std::int64_t bufferSize)
		: _data(static_cast<std::uint8_t*>(bufferPtr), bufferSize, [](std::uint8_t* data, std::size_t size) {}), _pos(0), _mode(AccessMode::Writable)
	{
		_size = bufferSize;
	}

	MemoryStream::MemoryStream(const void* bufferPtr, std::int64_t bufferSize)
		: _data(const_cast<std::uint8_t*>(static_cast<const std::uint8_t*>(bufferPtr)), bufferSize, [](std::uint8_t* data, std::size_t size) {}), _pos(0), _mode(AccessMode::ReadOnly)
	{
		_size = bufferSize;
	}

	MemoryStream::MemoryStream(ArrayView<const char> buffer)
		: MemoryStream(buffer.data(), static_cast<std::int64_t>(buffer.size()))
	{
	}

	MemoryStream::MemoryStream(ArrayView<const std::uint8_t> buffer)
		: MemoryStream(buffer.data(), static_cast<std::int64_t>(buffer.size()))
	{
	}

	void MemoryStream::Dispose()
	{
		_size = Stream::Invalid;
		_pos = 0;
		_mode = AccessMode::None;
	}

	std::int64_t MemoryStream::Seek(std::int64_t offset, SeekOrigin origin)
	{
		std::int64_t newPos;
		switch (origin) {
			case SeekOrigin::Begin: newPos = offset; break;
			case SeekOrigin::Current: newPos = _pos + offset; break;
			case SeekOrigin::End: newPos = _size + offset; break;
			default: return Stream::OutOfRange;
		}

		if (newPos < 0 || newPos > _size) {
			newPos = Stream::OutOfRange;
		} else {
			_pos = newPos;
		}
		return newPos;
	}

	std::int64_t MemoryStream::GetPosition() const
	{
		return _pos;
	}

	std::int64_t MemoryStream::Read(void* destination, std::int64_t bytesToRead)
	{
		std::int64_t bytesRead = 0;
		if (bytesToRead > 0 && _mode != AccessMode::None) {
			DEATH_ASSERT(destination != nullptr, "destination is null", 0);

			bytesRead = (_size < _pos + bytesToRead ? (_size - _pos) : bytesToRead);
			if (bytesRead > 0) {
				std::memcpy(destination, &_data[_pos], bytesRead);
				_pos += bytesRead;
			}
		}
		return bytesRead;
	}

	std::int64_t MemoryStream::Write(const void* source, std::int64_t bytesToWrite)
	{
		DEATH_ASSERT(source != nullptr, "source is null", 0);

		std::int64_t bytesWritten = 0;
		if (bytesToWrite > 0 && (_mode == AccessMode::Writable || _mode == AccessMode::Growable)) {
			if (_mode == AccessMode::Growable && _size < _pos + bytesToWrite) {
				_size = _pos + bytesToWrite;
				arrayResize(_data, Containers::NoInit, _size);
			}

			bytesWritten = (_pos + bytesToWrite > _size ? (_size - _pos) : bytesToWrite);
			if (bytesWritten > 0) {
				std::memcpy(&_data[_pos], source, bytesWritten);
				_pos += bytesWritten;
			}
		}
		return bytesWritten;
	}

	bool MemoryStream::Flush()
	{
		// Not supported
		return true;
	}

	bool MemoryStream::IsValid()
	{
		return (_mode != AccessMode::None);
	}

	std::int64_t MemoryStream::GetSize() const
	{
		return _size;
	}

	void MemoryStream::ReserveCapacity(std::int64_t bytes)
	{
		if (_mode == AccessMode::Growable) {
			arrayReserve(_data, _pos + bytes);
		}
	}

	std::int64_t MemoryStream::FetchFromStream(Stream& source, std::int64_t bytesToRead)
	{
		std::int64_t bytesReadTotal = 0;
		if (bytesToRead > 0 && (_mode == AccessMode::Writable || _mode == AccessMode::Growable)) {
			if (_size < _pos + bytesToRead) {
				if (_mode == AccessMode::Growable) {
					_size = _pos + bytesToRead;
					arrayResize(_data, Containers::NoInit, _size);
				} else {
					bytesToRead = static_cast<std::int32_t>(_size - _pos);
				}
			}

			while (bytesToRead > 0) {
				std::int64_t bytesRead = source.Read(&_data[_pos], bytesToRead);
				if DEATH_UNLIKELY(bytesRead <= 0) {
					break;
				}
				_pos += bytesRead;
				bytesReadTotal += bytesRead;
				bytesToRead -= bytesRead;
			}
		}
		return bytesReadTotal;
	}

}}
