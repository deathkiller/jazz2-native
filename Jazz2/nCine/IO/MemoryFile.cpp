#include "MemoryFile.h"

#include <cstring>

namespace nCine
{
	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	MemoryFile::MemoryFile(const String& bufferName, uint8_t* bufferPtr, uint32_t bufferSize)
		: IFileStream(bufferName), _bufferPtr(bufferPtr), _seekOffset(0), _isWritable(true)
	{
		ASSERT(bufferSize > 0);
		type_ = FileType::Memory;
		fileSize_ = bufferSize;

		// The memory file appears to be already opened when first created
		fileDescriptor_ = (bufferSize > 0) ? 0 : -1;
	}

	MemoryFile::MemoryFile(const String& bufferName, const uint8_t* bufferPtr, uint32_t bufferSize)
		: IFileStream(bufferName), _bufferPtr(const_cast<uint8_t*>(bufferPtr)), _seekOffset(0), _isWritable(false)
	{
		ASSERT(bufferSize > 0);
		type_ = FileType::Memory;
		fileSize_ = bufferSize;

		// The memory file appears to be already opened as read-only when first created
		fileDescriptor_ = (bufferSize > 0) ? 0 : -1;
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	void MemoryFile::Open(FileAccessMode mode, bool shouldExitOnFailToOpen)
	{
		// Checking if the file is already opened
		if (fileDescriptor_ >= 0) {
			LOGW_X("Memory file at address 0x%lx is already opened", _bufferPtr);
		} else {
			_isWritable = ((mode & FileAccessMode::Write) == FileAccessMode::Write);
		}
	}

	void MemoryFile::Close()
	{
		fileDescriptor_ = -1;
		_seekOffset = 0;
		_isWritable = false;
	}

	int32_t MemoryFile::Seek(int32_t offset, SeekOrigin origin) const
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

	int32_t MemoryFile::GetPosition() const
	{
		int32_t tellValue = -1;

		if (fileDescriptor_ >= 0)
			tellValue = _seekOffset;

		return tellValue;
	}

	uint32_t MemoryFile::Read(void* buffer, uint32_t bytes) const
	{
		ASSERT(buffer);

		uint32_t bytesRead = 0;

		if (fileDescriptor_ >= 0) {
			bytesRead = (_seekOffset + bytes > fileSize_) ? fileSize_ - _seekOffset : bytes;
			memcpy(buffer, _bufferPtr + _seekOffset, bytesRead);
			_seekOffset += bytesRead;
		}

		return bytesRead;
	}

	uint32_t MemoryFile::Write(const void* buffer, uint32_t bytes)
	{
		ASSERT(buffer);

		uint32_t bytesWritten = 0;

		if (fileDescriptor_ >= 0 && _isWritable) {
			bytesWritten = (_seekOffset + bytes > fileSize_) ? fileSize_ - _seekOffset : bytes;
			memcpy(_bufferPtr + _seekOffset, buffer, bytesWritten);
			_seekOffset += bytesWritten;
		}

		return bytesWritten;
	}

}
