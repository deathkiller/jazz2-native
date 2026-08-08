#pragma once

/** @file
	@brief Class @ref Death::IO::FileStream
*/

#include "Stream.h"
#include "FileAccess.h"
#include "../Containers/String.h"

#include <memory>

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Allows streaming from/to a file on a local filesystem

		If a file is opened with @ref FileAccess::Read or @ref FileAccess::ReadWrite, it must already exist.
		Otherwise the operation fails. If an existing file is opened with @ref FileAccess::Write, its content
		is destroyed and its size is set to 0. If the file doesn't exist, it is created.

		Reads and writes are buffered in blocks of @p bufferSize bytes. Specifying zero (or less) disables
		the buffering entirely, so every call is passed directly to the underlying file --- this is preferable
		if the caller already accumulates the data in a buffer of its own.

		The class is not thread-safe, an instance must not be used by more than one thread at a time.
	*/
	class FileStream : public Stream
	{
	public:
		/** @{ @name Constants */

		/** @brief Default buffer size for file access */
		static constexpr std::int32_t DefaultBufferSize = 8192;

		/** @} */

		FileStream(Containers::StringView path, FileAccess mode, std::int32_t bufferSize = DefaultBufferSize);
		FileStream(Containers::String&& path, FileAccess mode, std::int32_t bufferSize = DefaultBufferSize);
		~FileStream() override;

		FileStream(const FileStream&) = delete;
		FileStream& operator=(const FileStream&) = delete;

		void Dispose() override;
		std::int64_t Seek(std::int64_t offset, SeekOrigin origin) override;
		std::int64_t GetPosition() const override;
		std::int64_t Read(void* destination, std::int64_t bytesToRead) override;
		std::int64_t Write(const void* source, std::int64_t bytesToWrite) override;
		bool Flush() override;
		bool IsValid() override;
		std::int64_t GetSize() const override;
		std::int64_t SetSize(std::int64_t size) override;

		/** @brief Returns file path */
		Containers::StringView GetPath() const;

#if defined(DEATH_TARGET_WINDOWS)
		/** @brief Returns native file handle */
		DEATH_ALWAYS_INLINE void* GetHandle() const {
			return _fileHandle;
		}
#else
		/** @brief Returns native file descriptor */
		DEATH_ALWAYS_INLINE std::int32_t GetFileDescriptor() const {
			return _fileDescriptor;
		}
#endif

	private:
		Containers::String _path;
		std::int64_t _size;
		std::int64_t _filePos;
#if defined(DEATH_TARGET_WINDOWS)
		void* _fileHandle;
#else
		std::int32_t _fileDescriptor;
#endif
		std::int32_t _readPos;
		std::int32_t _readLength;
		std::int32_t _writePos;
		std::int32_t _bufferSize;
		std::unique_ptr<char[]> _buffer;
#if defined(DEATH_TARGET_DREAMCAST)
		// Points into _buffer at the first offset the storage driver can transfer to directly; only this
		// platform needs it, elsewhere the allocation's own base is already suitable (see InitializeBuffer())
		char* _bufferAligned = nullptr;
#endif

		/** @brief Returns the buffer address used for reads and writes */
		DEATH_ALWAYS_INLINE char* BufferForIo() const {
#if defined(DEATH_TARGET_DREAMCAST)
			return _bufferAligned;
#else
			return _buffer.get();
#endif
		}

		/**
			@brief Returns whether the file behaves like a regular file

			A known size is what distinguishes a regular file from a pipe, a socket or a terminal, because only
			the former reports its length when it's opened. Such a file is also seekable and a partial transfer
			means end of file instead of "no more data is available right now".
		*/
		DEATH_ALWAYS_INLINE bool IsRegularFile() const {
			return (_size >= 0);
		}

		void InitializeBuffer();
		void FlushReadBuffer();
		bool FlushWriteBuffer();

		void Open(FileAccess mode);
		std::int64_t SeekInternal(std::int64_t offset, SeekOrigin origin);
		std::int32_t ReadInternal(void* destination, std::int32_t bytesToRead);
		std::int32_t WriteInternal(const void* source, std::int32_t bytesToWrite);
	};
}}