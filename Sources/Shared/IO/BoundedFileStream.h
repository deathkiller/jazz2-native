#pragma once

#include "FileStream.h"

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Provides only specified portion of the specified seekable input stream
	*/
	class BoundedFileStream : public Stream
	{
	public:
		BoundedFileStream(Containers::StringView path, std::uint64_t offset, std::uint32_t size);
		BoundedFileStream(Containers::String&& path, std::uint64_t offset, std::uint32_t size);

		BoundedFileStream(const BoundedFileStream&) = delete;
		BoundedFileStream& operator=(const BoundedFileStream&) = delete;

		void Dispose() override;
		std::int64_t Seek(std::int64_t offset, SeekOrigin origin) override;
		std::int64_t GetPosition() const override;
		std::int64_t Read(void* destination, std::int64_t bytesToRead) override;
		std::int64_t Write(const void* source, std::int64_t bytesToWrite) override;
		bool Flush() override;
		bool IsValid() override;
		std::int64_t GetSize() const override;

	private:
		FileStream _underlyingStream;
		std::uint64_t _offset;
		std::uint64_t _size;
	};

}}