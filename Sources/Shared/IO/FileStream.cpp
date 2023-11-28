#include "FileStream.h"

#include "../CommonWindows.h"
#include "../Asserts.h"
#include "../Utf8.h"

#if defined(DEATH_TARGET_WINDOWS_RT)
#	include <fcntl.h>
#	include <io.h>
#elif defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_MINGW)
#	include <io.h>
#else
#	include <sys/stat.h>	// For open()
#	include <fcntl.h>		// For open()
#	include <unistd.h>		// For close()
#endif

namespace Death::IO
{
	FileStream::FileStream(const Containers::String& path, FileAccessMode mode)
		: _fileDescriptor(-1), _handle(nullptr), _shouldCloseOnDestruction(true)
	{
		_type = Type::File;
		_path = path;

#if !defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_MINGW)
		if ((mode & FileAccessMode::FileDescriptor) == FileAccessMode::FileDescriptor) {
			OpenDescriptor(mode);
		} else
#endif
		{
			OpenStream(mode);
		}
	}

	FileStream::~FileStream()
	{
		if (_shouldCloseOnDestruction) {
			Close();
		}
	}

	void FileStream::Close()
	{
		if (_fileDescriptor >= 0) {
#if !defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_MINGW)
			const int retValue = ::close(_fileDescriptor);
			if (retValue < 0) {
				LOGW("Cannot close the file \"%s\"", _path.data());
			} else {
				LOGI("File \"%s\" closed", _path.data());
				_fileDescriptor = -1;
			}
#endif
		} else if (_handle != nullptr) {
			const std::int32_t retValue = ::fclose(_handle);
			if (retValue == EOF) {
				LOGW("Cannot close the file \"%s\"", _path.data());
			} else {
				LOGI("File \"%s\" closed", _path.data());
				_handle = nullptr;
			}
		}
	}

	std::int32_t FileStream::Seek(std::int32_t offset, SeekOrigin origin)
	{
		std::int32_t seekValue = -1;

		if (_fileDescriptor >= 0) {
#if !defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_MINGW)
			seekValue = ::lseek(_fileDescriptor, offset, (std::int32_t)origin);
#endif
		} else if (_handle != nullptr) {
			seekValue = ::fseek(_handle, offset, (std::int32_t)origin);
		}
		return seekValue;
	}

	std::int32_t FileStream::GetPosition() const
	{
		std::int32_t tellValue = -1;

		if (_fileDescriptor >= 0) {
#if !defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_MINGW)
			tellValue = ::lseek(_fileDescriptor, 0L, SEEK_CUR);
#endif
		} else if (_handle != nullptr) {
			tellValue = ::ftell(_handle);
		}
		return tellValue;
	}

	std::int32_t FileStream::Read(void* buffer, std::int32_t bytes)
	{
		DEATH_ASSERT(buffer != nullptr, 0, "buffer is nullptr");

		std::int32_t bytesRead = 0;

		if (_fileDescriptor >= 0) {
#if !defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_MINGW)
			bytesRead = static_cast<std::int32_t>(::read(_fileDescriptor, buffer, bytes));
#endif
		} else if (_handle != nullptr) {
			bytesRead = static_cast<std::int32_t>(::fread(buffer, 1, bytes, _handle));
		}
		return bytesRead;
	}

	std::int32_t FileStream::Write(const void* buffer, std::int32_t bytes)
	{
		DEATH_ASSERT(buffer != nullptr, 0, "buffer is nullptr");

		std::int32_t bytesWritten = 0;

		if (_fileDescriptor >= 0) {
#if !defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_MINGW)
			bytesWritten = static_cast<std::int32_t>(::write(_fileDescriptor, buffer, bytes));
#endif
		} else if (_handle != nullptr) {
			bytesWritten = static_cast<std::int32_t>(::fwrite(buffer, 1, bytes, _handle));
		}
		return bytesWritten;
	}

#if !defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_MINGW)
	void FileStream::OpenDescriptor(FileAccessMode mode)
	{
		std::int32_t openFlag = -1;

		switch (mode) {
			case (FileAccessMode::FileDescriptor | FileAccessMode::Read):
				openFlag = O_RDONLY;
				break;
			case (FileAccessMode::FileDescriptor | FileAccessMode::Write):
				openFlag = O_WRONLY;
				break;
			case (FileAccessMode::FileDescriptor | FileAccessMode::Read | FileAccessMode::Write):
				openFlag = O_RDWR;
				break;
			default:
				LOGE("Cannot open the file \"%s\", wrong open mode", _path.data());
				break;
		}

		if (openFlag >= 0) {
			_fileDescriptor = ::open(_path.data(), openFlag);

			if (_fileDescriptor < 0) {
				LOGE("Cannot open the file \"%s\"", _path.data());
				return;
			}

			LOGI("File \"%s\" opened", _path.data());

			// Calculating file size
			_size = ::lseek(_fileDescriptor, 0L, SEEK_END);
			::lseek(_fileDescriptor, 0L, SEEK_SET);
		}
	}
#endif

	bool FileStream::IsValid() const
	{
		return (_handle != nullptr || _fileDescriptor >= 0);
	}

	void FileStream::OpenStream(FileAccessMode mode)
	{
#if defined(DEATH_TARGET_WINDOWS_RT)
		DWORD desireAccess, creationDisposition;
		std::int32_t openFlag;
		const char* modeInternal;
		switch (mode) {
			case FileAccessMode::Read:
				desireAccess = GENERIC_READ;
				creationDisposition = OPEN_EXISTING;
				openFlag = _O_RDONLY | _O_BINARY;
				modeInternal = "rb";
				break;
			case FileAccessMode::Write:
				desireAccess = GENERIC_WRITE;
				creationDisposition = CREATE_ALWAYS;
				openFlag = _O_WRONLY | _O_BINARY;
				modeInternal = "wb";
				break;
			case FileAccessMode::Read | FileAccessMode::Write:
				desireAccess = GENERIC_READ | GENERIC_WRITE;
				creationDisposition = OPEN_ALWAYS;
				openFlag = _O_RDWR | _O_BINARY;
				modeInternal = "r+b";
				break;
			default:
				LOGE("Cannot open the file \"%s\", wrong open mode", _path.data());
				return;
		}

		HANDLE hFile = ::CreateFile2FromAppW(Utf8::ToUtf16(_path), desireAccess, FILE_SHARE_READ, creationDisposition, nullptr);
		if (hFile == nullptr || hFile == INVALID_HANDLE_VALUE) {
			DWORD error = ::GetLastError();
			LOGE("Cannot open the file \"%s\" - failed with error 0x%08X", _path.data(), error);
			return;
		}
		// Automatically transfers ownership of the Win32 file handle to the file descriptor
		std::int32_t fd = _open_osfhandle(reinterpret_cast<std::intptr_t>(hFile), openFlag);
		_handle = _fdopen(fd, modeInternal);
#elif defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_MINGW)
		const wchar_t* modeInternal;
		std::int32_t shareMode;
		switch (mode) {
			case FileAccessMode::Read: modeInternal = L"rb"; shareMode = _SH_DENYNO; break;
			case FileAccessMode::Write: modeInternal = L"wb"; shareMode = _SH_DENYWR; break;
			case FileAccessMode::Read | FileAccessMode::Write: modeInternal = L"r+b"; shareMode = _SH_DENYWR; break;
			default:
				LOGE("Cannot open the file \"%s\", wrong open mode", _path.data());
				return;
		}

		_handle = _wfsopen(Utf8::ToUtf16(_path), modeInternal, shareMode);
#else
		const char* modeInternal;
		switch (mode) {
			case FileAccessMode::Read: modeInternal = "rb"; break;
			case FileAccessMode::Write: modeInternal = "wb"; break;
			case FileAccessMode::Read | FileAccessMode::Write: modeInternal = "r+b"; break;
			default:
				LOGE("Cannot open the file \"%s\", wrong open mode", _path.data());
				return;
		}

		_handle = ::fopen(_path.data(), modeInternal);
#endif

		if (_handle == nullptr) {
			LOGE("Cannot open the file \"%s\"", _path.data());
			return;
		}

		LOGI("File \"%s\" opened", _path.data());

		// Calculating file size
		::fseek(_handle, 0L, SEEK_END);
		_size = static_cast<std::int32_t>(::ftell(_handle));
		::fseek(_handle, 0L, SEEK_SET);
	}
}
