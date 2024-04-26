#include "FileStream.h"

#include "../CommonWindows.h"
#include "../Asserts.h"
#include "../Utf8.h"

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_MINGW)
#	include <io.h>
#	if defined(DEATH_TARGET_WINDOWS_RT)
#		include <fcntl.h>
#	endif
#else
#	include <cerrno>
#	include <sys/stat.h>	// For open()
#	include <fcntl.h>		// For open()
#	include <unistd.h>		// For close()
#endif

// `_nolock` functions are not supported by VC-LTL, `_unlocked` functions are not supported on Android and Apple
#if (defined(DEATH_TARGET_WINDOWS) && !defined(_Build_By_LTL)) || (!defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_APPLE))
#	define DEATH_USE_NOLOCK_IN_FILE
#endif

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	FileStream::FileStream(const Containers::StringView path, FileAccessMode mode)
		: FileStream(Containers::String{path}, mode)
	{
	}

	FileStream::FileStream(Containers::String&& path, FileAccessMode mode)
		: _shouldCloseOnDestruction(true), _path(std::move(path)),
#if defined(DEATH_USE_FILE_DESCRIPTORS)
		_fileDescriptor(-1)
#else
		_handle(nullptr)
#endif
	{
		Open(mode);
	}

	FileStream::~FileStream()
	{
		if (_shouldCloseOnDestruction) {
			FileStream::Close();
		}
	}

	void FileStream::Close()
	{
#if defined(DEATH_USE_FILE_DESCRIPTORS)
		if (_fileDescriptor >= 0) {
			const std::int32_t retValue = ::close(_fileDescriptor);
			if (retValue < 0) {
				LOGW("Cannot close the file \"%s\"", _path.data());
			} else {
				LOGI("File \"%s\" closed", _path.data());
				_fileDescriptor = -1;
			}
		}
#else
		if (_handle != nullptr) {
			const std::int32_t retValue = ::fclose(_handle);
			if (retValue == EOF) {
				LOGW("Cannot close the file \"%s\"", _path.data());
			} else {
				LOGI("File \"%s\" closed", _path.data());
				_handle = nullptr;
			}
		}
#endif
	}

	std::int64_t FileStream::Seek(std::int64_t offset, SeekOrigin origin)
	{
		std::int64_t newPos = ErrorInvalidStream;
#if defined(DEATH_USE_FILE_DESCRIPTORS)
		if (_fileDescriptor >= 0) {
			newPos = ::lseek(_fileDescriptor, offset, static_cast<std::int32_t>(origin));
		}
#else
		if (_handle != nullptr) {
			// ::fseek return 0 on success
#	if defined(DEATH_TARGET_WINDOWS)
#		if defined(DEATH_USE_NOLOCK_IN_FILE)
			if (::_fseeki64_nolock(_handle, offset, static_cast<std::int32_t>(origin)) == 0) {
				newPos = ::_ftelli64_nolock(_handle);
			}
#		else
			if (::_fseeki64(_handle, offset, static_cast<std::int32_t>(origin)) == 0) {
				newPos = ::_ftelli64(_handle);
			}
#		endif
#	else
			if (::fseeko(_handle, offset, static_cast<std::int32_t>(origin)) == 0) {
				newPos = ::ftello(_handle);
			}
#	endif
			else {
				newPos = ErrorInvalidParameter;
			}
		}
#endif
		return newPos;
	}

	std::int64_t FileStream::GetPosition() const
	{
		std::int64_t pos = ErrorInvalidStream;
#if defined(DEATH_USE_FILE_DESCRIPTORS)
		if (_fileDescriptor >= 0) {
			pos = ::lseek(_fileDescriptor, 0L, SEEK_CUR);
		}
#else
		if (_handle != nullptr) {
#	if defined(DEATH_TARGET_WINDOWS)
#		if defined(DEATH_USE_NOLOCK_IN_FILE)
			pos = ::_ftelli64_nolock(_handle);
#		else
			pos = ::_ftelli64(_handle);
#		endif
#	else
			pos = ::ftello(_handle);
#	endif
		}
#endif
		return pos;
	}

	std::int32_t FileStream::Read(void* buffer, std::int32_t bytes)
	{
		DEATH_ASSERT(buffer != nullptr, 0, "buffer is nullptr");

		std::int32_t bytesRead = 0;
#if defined(DEATH_USE_FILE_DESCRIPTORS)
		if (_fileDescriptor >= 0) {
			bytesRead = static_cast<std::int32_t>(::read(_fileDescriptor, buffer, bytes));
		}
#else
		if (_handle != nullptr) {
#	if defined(DEATH_TARGET_WINDOWS)
#		if defined(DEATH_USE_NOLOCK_IN_FILE)
			bytesRead = static_cast<std::int32_t>(::_fread_nolock(buffer, 1, bytes, _handle));
#		else
			bytesRead = static_cast<std::int32_t>(::fread(buffer, 1, bytes, _handle));
#		endif
#	elif defined(DEATH_USE_NOLOCK_IN_FILE)
			bytesRead = static_cast<std::int32_t>(::fread_unlocked(buffer, 1, bytes, _handle));
#	else
			bytesRead = static_cast<std::int32_t>(::fread(buffer, 1, bytes, _handle));
#	endif
		}
#endif
		return bytesRead;
	}

	std::int32_t FileStream::Write(const void* buffer, std::int32_t bytes)
	{
		DEATH_ASSERT(buffer != nullptr, 0, "buffer is nullptr");

		std::int32_t bytesWritten = 0;
#if defined(DEATH_USE_FILE_DESCRIPTORS)
		if (_fileDescriptor >= 0) {
			bytesWritten = static_cast<std::int32_t>(::write(_fileDescriptor, buffer, bytes));
		}
#else
		if (_handle != nullptr) {
#	if defined(DEATH_TARGET_WINDOWS)
#		if defined(DEATH_USE_NOLOCK_IN_FILE)
			bytesWritten = static_cast<std::int32_t>(::_fwrite_nolock(buffer, 1, bytes, _handle));
#		else
			bytesWritten = static_cast<std::int32_t>(::fwrite(buffer, 1, bytes, _handle));
#		endif
#	elif defined(DEATH_USE_NOLOCK_IN_FILE)
			bytesWritten = static_cast<std::int32_t>(::fwrite_unlocked(buffer, 1, bytes, _handle));
#	else
			bytesWritten = static_cast<std::int32_t>(::fwrite(buffer, 1, bytes, _handle));
#	endif
		}
#endif
		return bytesWritten;
	}

	bool FileStream::IsValid()
	{
#if defined(DEATH_USE_FILE_DESCRIPTORS)
		return (_fileDescriptor >= 0);
#else
		return (_handle != nullptr);
#endif
	}

	Containers::StringView FileStream::GetPath() const
	{
		return _path;
	}

	void FileStream::Open(FileAccessMode mode)
	{
#if defined(DEATH_USE_FILE_DESCRIPTORS)
		std::int32_t openFlag;
		switch (mode & ~FileAccessMode::Exclusive) {
			case FileAccessMode::Read:
				openFlag = O_RDONLY;
				break;
			case FileAccessMode::Write:
				openFlag = O_WRONLY;
				break;
			case FileAccessMode::Read | FileAccessMode::Write:
				openFlag = O_RDWR;
				break;
			default:
				LOGE("Cannot open file \"%s\" - wrong open mode", _path.data());
				return;
		}

		_fileDescriptor = ::open(_path.data(), openFlag);
		if (_fileDescriptor < 0) {
			LOGE("Cannot open file \"%s\" - failed with error %i", _path.data(), errno);
			return;
		}

		switch (mode & ~FileAccessMode::Exclusive) {
			default: LOGI("File \"%s\" opened", _path.data()); break;
			case FileAccessMode::Write: LOGI("File \"%s\" opened for write", _path.data()); break;
			case FileAccessMode::Read | FileAccessMode::Write: LOGI("File \"%s\" opened for read+write", _path.data()); break;
		}

		// Try to get file size
		_size = ::lseek(_fileDescriptor, 0, SEEK_END);
		::lseek(_fileDescriptor, 0, SEEK_SET);
#else
#	if defined(DEATH_TARGET_WINDOWS_RT)
		DWORD desireAccess, creationDisposition;
		std::int32_t openFlag;
		const char* modeInternal;
		DWORD shareMode;
		switch (mode & ~FileAccessMode::Exclusive) {
			case FileAccessMode::Read:
				desireAccess = GENERIC_READ;
				creationDisposition = OPEN_EXISTING;
				openFlag = _O_RDONLY | _O_BINARY;
				modeInternal = "rb";
				shareMode = ((mode & FileAccessMode::Exclusive) == FileAccessMode::Exclusive ? 0 : FILE_SHARE_READ | FILE_SHARE_WRITE);
				break;
			case FileAccessMode::Write:
				desireAccess = GENERIC_WRITE;
				creationDisposition = CREATE_ALWAYS;
				openFlag = _O_WRONLY | _O_BINARY;
				modeInternal = "wb";
				shareMode = ((mode & FileAccessMode::Exclusive) == FileAccessMode::Exclusive ? 0 : FILE_SHARE_READ);
				break;
			case FileAccessMode::Read | FileAccessMode::Write:
				desireAccess = GENERIC_READ | GENERIC_WRITE;
				creationDisposition = OPEN_ALWAYS;
				openFlag = _O_RDWR | _O_BINARY;
				modeInternal = "r+b";
				shareMode = ((mode & FileAccessMode::Exclusive) == FileAccessMode::Exclusive ? 0 : FILE_SHARE_READ);
				break;
			default:
				LOGE("Cannot open file \"%s\" - wrong open mode", _path.data());
				return;
		}

		HANDLE hFile = ::CreateFile2FromAppW(Utf8::ToUtf16(_path), desireAccess, shareMode, creationDisposition, nullptr);
		if (hFile == nullptr || hFile == INVALID_HANDLE_VALUE) {
			DWORD error = ::GetLastError();
			LOGE("Cannot open file \"%s\" - failed with error 0x%08X", _path.data(), error);
			return;
		}
		// Automatically transfers ownership of the Win32 file handle to the file descriptor
		std::int32_t fd = _open_osfhandle(reinterpret_cast<std::intptr_t>(hFile), openFlag);
		_handle = _fdopen(fd, modeInternal);
		if (_handle == nullptr) {
			LOGE("Cannot open file \"%s\" - failed with internal error", _path.data());
			return;
		}
#	elif defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_MINGW)
		const wchar_t* modeInternal;
		std::int32_t shareMode;
		switch (mode & ~FileAccessMode::Exclusive) {
			case FileAccessMode::Read: modeInternal = L"rb"; shareMode = ((mode & FileAccessMode::Exclusive) == FileAccessMode::Exclusive ? _SH_DENYRW : _SH_DENYNO); break;
			case FileAccessMode::Write: modeInternal = L"wb"; shareMode = ((mode & FileAccessMode::Exclusive) == FileAccessMode::Exclusive ? _SH_DENYRW : _SH_DENYWR); break;
			case FileAccessMode::Read | FileAccessMode::Write: modeInternal = L"r+b"; shareMode = ((mode & FileAccessMode::Exclusive) == FileAccessMode::Exclusive ? _SH_DENYRW : _SH_DENYWR); break;
			default:
				LOGE("Cannot open file \"%s\" - wrong open mode", _path.data());
				return;
		}

		_handle = _wfsopen(Utf8::ToUtf16(_path), modeInternal, shareMode);
		if (_handle == nullptr) {
			DWORD error = ::GetLastError();
			LOGE("Cannot open file \"%s\" - failed with error 0x%08X", _path.data(), error);
			return;
		}
#	else
		const char* modeInternal;
		switch (mode & ~FileAccessMode::Exclusive) {
			case FileAccessMode::Read: modeInternal = "rb"; break;
			case FileAccessMode::Write: modeInternal = "wb"; break;
			case FileAccessMode::Read | FileAccessMode::Write: modeInternal = "r+b"; break;
			default:
				LOGE("Cannot open file \"%s\" - wrong open mode", _path.data());
				return;
		}

		_handle = ::fopen(_path.data(), modeInternal);
		if (_handle == nullptr) {
			LOGE("Cannot open file \"%s\" - failed with error %i", _path.data(), errno);
			return;
		}
#	endif

		switch (mode & ~FileAccessMode::Exclusive) {
			default: LOGI("File \"%s\" opened", _path.data()); break;
			case FileAccessMode::Write: LOGI("File \"%s\" opened for write", _path.data()); break;
			case FileAccessMode::Read | FileAccessMode::Write: LOGI("File \"%s\" opened for read+write", _path.data()); break;
		}

		// Try to get file size
#	if defined(DEATH_TARGET_WINDOWS)
#		if defined(DEATH_USE_NOLOCK_IN_FILE)
		::_fseeki64_nolock(_handle, 0, SEEK_END);
		_size = ::_ftelli64_nolock(_handle);
		::_fseeki64_nolock(_handle, 0, SEEK_SET);
#		else
		::_fseeki64(_handle, 0, SEEK_END);
		_size = ::_ftelli64(_handle);
		::_fseeki64(_handle, 0, SEEK_SET);
#		endif
#	else
		::fseeko(_handle, 0, SEEK_END);
		_size = ::ftello(_handle);
		::fseeko(_handle, 0, SEEK_SET);
#	endif
#endif
	}

}}
