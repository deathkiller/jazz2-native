#pragma once

#include "IFileStream.h"

namespace nCine
{
	/// The class creating a file interface around a memory buffer
	class MemoryFile : public IFileStream
	{
	public:
		MemoryFile(const String& bufferName, unsigned char* bufferPtr, unsigned long int bufferSize);
		MemoryFile(const String& bufferName, const unsigned char* bufferPtr, unsigned long int bufferSize);
		MemoryFile(unsigned char* bufferPtr, unsigned long int bufferSize);
		MemoryFile(const unsigned char* bufferPtr, unsigned long int bufferSize);

		void Open(FileAccessMode mode) override;
		void Close() override;
		long int Seek(long int offset, SeekOrigin origin) const override;
		long int GetPosition() const override;
		unsigned long int Read(void* buffer, unsigned long int bytes) const override;
		unsigned long int Write(const void* buffer, unsigned long int bytes) override;

	private:
		unsigned char* bufferPtr_;
		/// \note Modified by `seek` and `tell` constant methods
		mutable unsigned long int seekOffset_;
		bool isWritable_;

		/// Deleted copy constructor
		MemoryFile(const MemoryFile&) = delete;
		/// Deleted assignment operator
		MemoryFile& operator=(const MemoryFile&) = delete;
	};

}
