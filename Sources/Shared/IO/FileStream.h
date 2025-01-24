#pragma once

#include "Stream.h"
#include "FileAccess.h"
#include "../Containers/String.h"

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Allows streaming from/to a file on a local filesystem
	*/
	class FileStream : public Stream
	{
	public:
		FileStream(Containers::StringView path, FileAccess mode, std::int32_t bufferSize = 8192);
		FileStream(Containers::String&& path, FileAccess mode, std::int32_t bufferSize = 8192);
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

		void InitializeBuffer();
		void FlushReadBuffer();
		void FlushWriteBuffer();

		void Open(FileAccess mode);
		std::int64_t SeekInternal(std::int64_t offset, SeekOrigin origin);
		std::int32_t ReadInternal(void* destination, std::int32_t bytesToRead);
		std::int32_t WriteInternal(const void* source, std::int32_t bytesToWrite);
	};
}}