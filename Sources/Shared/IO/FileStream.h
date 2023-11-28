#pragma once

#include "Stream.h"

namespace Death::IO
{
	/** @brief The class handling opening, reading and closing a standard file from native filesystem */
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

		/** @brief Returns file descriptor */
		DEATH_ALWAYS_INLINE std::int32_t GetFileDescriptor() const {
			return _fileDescriptor;
		}
		/** @brief Returns file stream pointer */
		DEATH_ALWAYS_INLINE FILE* GetHandle() const {
			return _handle;
		}

	private:
		FileStream(const FileStream&) = delete;
		FileStream& operator=(const FileStream&) = delete;

		std::int32_t _fileDescriptor;
		FILE* _handle;
		bool _shouldCloseOnDestruction;

#if !defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_MINGW)
		void OpenDescriptor(FileAccessMode mode);
#endif
		void OpenStream(FileAccessMode mode);
	};

}

