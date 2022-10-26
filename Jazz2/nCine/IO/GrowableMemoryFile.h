#pragma once

#include "IFileStream.h"

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	/// The class creating a file interface around a growable memory buffer
	class GrowableMemoryFile : public IFileStream
	{
	public:
		GrowableMemoryFile();
		GrowableMemoryFile(int32_t initialCapacity);

		void Open(FileAccessMode mode) override;
		void Close() override;
		int32_t Seek(int32_t offset, SeekOrigin origin) const override;
		int32_t GetPosition() const override;
		uint32_t Read(void* buffer, uint32_t bytes) const override;
		uint32_t Write(const void* buffer, uint32_t bytes) override;

		const uint8_t* GetBuffer() {
			return &_buffer[0];
		}

	private:
		mutable SmallVector<uint8_t, 0> _buffer;
		mutable uint32_t _seekOffset;

		/// Deleted copy constructor
		GrowableMemoryFile(const GrowableMemoryFile&) = delete;
		/// Deleted assignment operator
		GrowableMemoryFile& operator=(const GrowableMemoryFile&) = delete;
	};

}
