#include "MemoryFile.h"

namespace nCine {

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	MemoryFile::MemoryFile(const char* bufferName, unsigned char* bufferPtr, unsigned long int bufferSize)
		: IFileStream(bufferName), bufferPtr_(bufferPtr), seekOffset_(0), isWritable_(true)
	{
		//ASSERT(bufferSize > 0);
		type_ = FileType::Memory;
		fileSize_ = bufferSize;

		// The memory file appears to be already opened when first created
		fileDescriptor_ = (bufferSize > 0) ? 0 : -1;
	}

	MemoryFile::MemoryFile(const char* bufferName, const unsigned char* bufferPtr, unsigned long int bufferSize)
		: IFileStream(bufferName), bufferPtr_(const_cast<unsigned char*>(bufferPtr)), seekOffset_(0), isWritable_(false)
	{
		//ASSERT(bufferSize > 0);
		type_ = FileType::Memory;
		fileSize_ = bufferSize;

		// The memory file appears to be already opened as read-only when first created
		fileDescriptor_ = (bufferSize > 0) ? 0 : -1;
	}

	MemoryFile::MemoryFile(unsigned char* bufferPtr, unsigned long int bufferSize)
		: MemoryFile("MemoryFile", bufferPtr, bufferSize)
	{
	}

	MemoryFile::MemoryFile(const unsigned char* bufferPtr, unsigned long int bufferSize)
		: MemoryFile("MemoryFile", bufferPtr, bufferSize)
	{
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	void MemoryFile::Open(FileAccessMode mode)
	{
		// Checking if the file is already opened
		if (fileDescriptor_ >= 0) {
			LOGW_X("Memory file \"%s\" at address 0x%lx is already opened", filename_.data(), bufferPtr_);
		} else {
			isWritable_ = ((mode & FileAccessMode::Write) == FileAccessMode::Write);
		}
	}

	void MemoryFile::Close()
	{
		fileDescriptor_ = -1;
		seekOffset_ = 0;
		isWritable_ = false;
	}

	long int MemoryFile::Seek(long int offset, SeekOrigin origin) const
	{
		long int seekValue = -1;

		if (fileDescriptor_ >= 0) {
			switch (origin) {
				case SeekOrigin::Begin:
					seekValue = offset;
					break;
				case SeekOrigin::Current:
					seekValue = seekOffset_ + offset;
					break;
				case SeekOrigin::End:
					seekValue = fileSize_ + offset;
					break;
			}
		}

		if (seekValue < 0 || seekValue > static_cast<long int>(fileSize_)) {
			seekValue = -1;
		} else {
			seekOffset_ = seekValue;
		}
		return seekValue;
	}

	long int MemoryFile::GetPosition() const
	{
		long int tellValue = -1;

		if (fileDescriptor_ >= 0)
			tellValue = seekOffset_;

		return tellValue;
	}

	unsigned long int MemoryFile::Read(void* buffer, unsigned long int bytes) const
	{
		//ASSERT(buffer);

		unsigned long int bytesRead = 0;

		if (fileDescriptor_ >= 0) {
			bytesRead = (seekOffset_ + bytes > fileSize_) ? fileSize_ - seekOffset_ : bytes;
			memcpy(buffer, bufferPtr_ + seekOffset_, bytesRead);
			seekOffset_ += bytesRead;
		}

		return bytesRead;
	}

	unsigned long int MemoryFile::Write(void* buffer, unsigned long int bytes)
	{
		//ASSERT(buffer);

		unsigned long int bytesWritten = 0;

		if (fileDescriptor_ >= 0 && isWritable_) {
			bytesWritten = (seekOffset_ + bytes > fileSize_) ? fileSize_ - seekOffset_ : bytes;
			memcpy(bufferPtr_ + seekOffset_, buffer, bytesWritten);
			seekOffset_ += bytesWritten;
		}

		return bytesWritten;
	}

}
