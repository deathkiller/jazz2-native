#pragma once

/** @file
	@brief Class @ref Death::IO::BoundedFileStream
*/

#include "FileStream.h"
#include "FileStreamPool.h"

#include <memory>

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Provides only specified portion of the specified seekable input stream
	*/
	class BoundedFileStream : public Stream
	{
	public:
		BoundedFileStream(Containers::StringView path, std::uint64_t offset, std::uint32_t size, std::int32_t bufferSize = FileStream::DefaultBufferSize);
		BoundedFileStream(Containers::String&& path, std::uint64_t offset, std::uint32_t size, std::int32_t bufferSize = FileStream::DefaultBufferSize);
		/**
			@brief Borrows the underlying stream from a pool instead of opening the file by path

			The stream goes back to the pool in @ref Dispose() (or the destructor), keeping the file open for the
			next reader - see @ref FileStreamPool for why an archive wants this. The pool is shared, so this stream
			may outlive the archive that created it; the pool then simply closes with the last stream.
		*/
		BoundedFileStream(std::shared_ptr<FileStreamPool> pool, std::uint64_t offset, std::uint32_t size, std::int32_t bufferSize = FileStream::DefaultBufferSize);
		~BoundedFileStream() override;

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
		std::int64_t SetSize(std::int64_t size) override;

	private:
		std::unique_ptr<FileStream> _underlyingStream;
		std::shared_ptr<FileStreamPool> _pool;
		std::uint64_t _offset;
		std::uint64_t _size;
	};

}}
