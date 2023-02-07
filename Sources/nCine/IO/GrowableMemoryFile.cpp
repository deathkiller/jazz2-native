#include "GrowableMemoryFile.h"

#include <cstring>

namespace nCine
{
	GrowableMemoryFile::GrowableMemoryFile()
		: IFileStream(nullptr), _seekOffset(0)
	{
	}

	GrowableMemoryFile::GrowableMemoryFile(int32_t initialCapacity)
		: IFileStream(nullptr), _seekOffset(0)
	{
		ASSERT(initialCapacity > 0);
		_buffer.reserve(initialCapacity);
	}

	void GrowableMemoryFile::Open(FileAccessMode mode)
	{
	}

	void GrowableMemoryFile::Close()
	{
	}

	int32_t GrowableMemoryFile::Seek(int32_t offset, SeekOrigin origin) const
	{
		int32_t seekValue = -1;

		if (fileDescriptor_ >= 0) {
			switch (origin) {
				case SeekOrigin::Begin:
					seekValue = offset;
					break;
				case SeekOrigin::Current:
					seekValue = _seekOffset + offset;
					break;
				case SeekOrigin::End:
					seekValue = fileSize_ + offset;
					break;
			}
		}

		if (seekValue < 0 || seekValue > static_cast<int32_t>(fileSize_)) {
			seekValue = -1;
		} else {
			_seekOffset = seekValue;
		}
		return seekValue;
	}

	int32_t GrowableMemoryFile::GetPosition() const
	{
		int32_t tellValue = -1;

		if (fileDescriptor_ >= 0) {
			tellValue = _seekOffset;
		}

		return tellValue;
	}

	uint32_t GrowableMemoryFile::Read(void* buffer, uint32_t bytes) const
	{
		if (bytes > 0) {
			uint32_t bytesRead = (_seekOffset + bytes > fileSize_) ? fileSize_ - _seekOffset : bytes;
			memcpy(buffer, _buffer.begin() + _seekOffset, bytesRead);
			_seekOffset += bytesRead;
			return bytesRead;
		} else {
			return 0;
		}
	}

	uint32_t GrowableMemoryFile::Write(const void* buffer, uint32_t bytes)
	{
		if (bytes > 0) {
			if (fileSize_ < _seekOffset + bytes) {
				fileSize_ = _seekOffset + bytes;
				_buffer.resize_for_overwrite(fileSize_);
			}

			memcpy(_buffer.begin() + _seekOffset, buffer, bytes);
			_seekOffset += bytes;
		}
		return bytes;
	}
}
