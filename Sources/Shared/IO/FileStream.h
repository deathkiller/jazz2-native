#pragma once

#include "Stream.h"

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Streaming from/to a file on a local filesystem
	*/
	class FileStream : public Stream
	{
	public:
		explicit FileStream(const Containers::String& path, FileAccessMode mode);
		~FileStream() override;

		void Close() override;
		std::int32_t Seek(std::int32_t offset, SeekOrigin origin) override;
		std::int32_t GetPosition() const override;
		std::int32_t Read(void* buffer, std::int32_t bytes) override;
		std::int32_t Write(const void* buffer, std::int32_t bytes) override;

		bool IsValid() const override;

		void SetCloseOnDestruction(bool shouldCloseOnDestruction) override {
			_shouldCloseOnDestruction = shouldCloseOnDestruction;
		}

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
		FileStream(const FileStream&) = delete;
		FileStream& operator=(const FileStream&) = delete;

#if defined(DEATH_USE_FILE_DESCRIPTORS)
		std::int32_t _fileDescriptor;
#else
		FILE* _handle;
#endif
		bool _shouldCloseOnDestruction;

		void Open(FileAccessMode mode);
	};
}}