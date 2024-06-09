#pragma once

#include "Stream.h"
#include "FileAccess.h"
#include "../Containers/String.h"

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Streaming from/to a file on a local filesystem
	*/
	class FileStream : public Stream
	{
	public:
		FileStream(const Containers::StringView path, FileAccess mode);
		FileStream(Containers::String&& path, FileAccess mode);
		~FileStream() override;

		FileStream(const FileStream&) = delete;
		FileStream& operator=(const FileStream&) = delete;

		void Dispose() override;
		std::int64_t Seek(std::int64_t offset, SeekOrigin origin) override;
		std::int64_t GetPosition() const override;
		std::int32_t Read(void* buffer, std::int32_t bytes) override;
		std::int32_t Write(const void* buffer, std::int32_t bytes) override;
		bool Flush() override;
		bool IsValid() override;
		std::int64_t GetSize() const override;

		/** @brief Returns file path */
		Containers::StringView GetPath() const;

#if defined(DEATH_USE_FILE_DESCRIPTORS)
		/** @brief Returns file descriptor */
		DEATH_ALWAYS_INLINE std::int32_t GetFileDescriptor() const {
			return _fileDescriptor;
		}
#else
		/** @brief Returns file stream pointer */
		DEATH_ALWAYS_INLINE FILE* GetHandle() const {
			return _handle;
		}
#endif

	private:
		Containers::String _path;
		std::int64_t _size;
#if defined(DEATH_USE_FILE_DESCRIPTORS)
		std::int32_t _fileDescriptor;
#else
		FILE* _handle;
#endif

		void Open(FileAccess mode);
	};
}}