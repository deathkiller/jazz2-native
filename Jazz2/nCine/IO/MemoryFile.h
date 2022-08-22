#pragma once

#include "IFileStream.h"

namespace nCine
{
	/// The class creating a file interface around a memory buffer
	class MemoryFile : public IFileStream
	{
	public:
		MemoryFile(const String& bufferName, uint8_t* bufferPtr, uint32_t bufferSize);
		MemoryFile(const String& bufferName, const uint8_t* bufferPtr, uint32_t bufferSize);

		void Open(FileAccessMode mode, bool shouldExitOnFailToOpen) override;
		void Close() override;
		int32_t Seek(int32_t offset, SeekOrigin origin) const override;
		int32_t GetPosition() const override;
		uint32_t Read(void* buffer, uint32_t bytes) const override;
		uint32_t Write(const void* buffer, uint32_t bytes) override;

	private:
		uint8_t* _bufferPtr;
		/// \note Modified by `seek` and `tell` constant methods
		mutable uint32_t _seekOffset;
		bool _isWritable;

		/// Deleted copy constructor
		MemoryFile(const MemoryFile&) = delete;
		/// Deleted assignment operator
		MemoryFile& operator=(const MemoryFile&) = delete;
	};

}
