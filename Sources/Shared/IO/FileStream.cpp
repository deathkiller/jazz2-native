#include "FileStream.h"

#include "../CommonWindows.h"
#include "../Asserts.h"
#include "../Containers/Array.h"
#include "../Utf8.h"

#include <cstring>

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_MINGW)
#	include <io.h>
#	if defined(DEATH_TARGET_WINDOWS_RT)
#		include <fcntl.h>
#	endif
#else
#	include <cerrno>
#	include <sys/stat.h>	// for open()
#	include <fcntl.h>		// for open()
#	include <unistd.h>		// for close()
#endif

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

#if defined(DEATH_TARGET_WINDOWS)
	const char* __GetWin32ErrorSuffix(DWORD error)
	{
		switch (error) {
			case ERROR_FILE_NOT_FOUND: return " (FILE_NOT_FOUND)"; break;
			case ERROR_PATH_NOT_FOUND: return " (PATH_NOT_FOUND)"; break;
			case ERROR_ACCESS_DENIED: return " (ACCESS_DENIED)"; break;
			case ERROR_SHARING_VIOLATION: return " (SHARING_VIOLATION)"; break;
			case ERROR_INVALID_PARAMETER: return " (INVALID_PARAMETER)"; break;
			case ERROR_DISK_FULL: return " (DISK_FULL)"; break;
			case ERROR_INVALID_NAME: return " (INVALID_NAME)"; break;
			default: return ""; break;
		}
	}
#else
	const char* __GetUnixErrorSuffix(std::int32_t error)
	{
		switch (error) {
			case EPERM: return " (Operation not permitted)"; break;
			case ENOENT: return " (No such file or directory)"; break;
			case EIO: return " (Input/output error)"; break;
			case ENXIO: return " (No such device or address)"; break;
			case EACCES: return " (Permission denied)"; break;
			case EBUSY: return " (Device or resource busy)"; break;
			case EEXIST: return " (File exists)"; break;
			case ENODEV: return " (No such device)"; break;
			case ENOTDIR: return " (Not a directory)"; break;
			case EISDIR: return " (Is a directory)"; break;
			case EINVAL: return " (Invalid argument)"; break;
			case EFBIG: return " (File too large)"; break;
			case ENOSPC: return " (No space left on device)"; break;
			case ESPIPE: return " (Illegal seek)"; break;
			case EROFS: return " (Read-only file system)"; break;
			case EPIPE: return " (Broken pipe)"; break;
			case ENOTEMPTY: return " (Directory not empty)"; break;
			default: return ""; break;
		}
	}
#endif

	FileStream::FileStream(Containers::StringView path, FileAccess mode, std::int32_t bufferSize)
		: FileStream(Containers::String{path}, mode, bufferSize)
	{
	}

	FileStream::FileStream(Containers::String&& path, FileAccess mode, std::int32_t bufferSize)
		: _path(Death::move(path)), _size(Stream::Invalid), _filePos(0), _readPos(0), _readLength(0), _writePos(0), _bufferSize(bufferSize),
#if defined(DEATH_TARGET_WINDOWS)
			_fileHandle(INVALID_HANDLE_VALUE)

#else
			_fileDescriptor(-1)
#endif
	{
		Open(mode);
	}

	FileStream::~FileStream()
	{
		FileStream::Dispose();
	}

	void FileStream::Dispose()
	{
		FlushWriteBuffer();

#if defined(DEATH_TARGET_WINDOWS)
		if (_fileHandle != INVALID_HANDLE_VALUE) {
			if (::CloseHandle(_fileHandle)) {
#	if defined(DEATH_TRACE_VERBOSE_IO)
				LOGB("File \"%s\" closed", _path.data());
#	endif
				_fileHandle = INVALID_HANDLE_VALUE;
			} else {
#	if defined(DEATH_TRACE_VERBOSE_IO)
				LOGW("Cannot close file \"%s\"", _path.data());
#	endif
			}
		}
#else
		if (_fileDescriptor >= 0) {
			const std::int32_t retValue = ::close(_fileDescriptor);
			if (retValue >= 0) {
#	if defined(DEATH_TRACE_VERBOSE_IO)
				LOGB("File \"%s\" closed", _path.data());
#	endif
				_fileDescriptor = -1;
			} else {
#	if defined(DEATH_TRACE_VERBOSE_IO)
				LOGW("Cannot close file \"%s\"", _path.data());
#	endif
			}
		}
#endif
	}

	std::int64_t FileStream::Seek(std::int64_t offset, SeekOrigin origin)
	{
		if (_writePos > 0) {
			FlushWriteBuffer();
		} else if (origin == SeekOrigin::Current) {
			offset -= (_readLength - _readPos);
		}

		std::int64_t oldPos = _filePos + (_readPos - _readLength);
		std::int64_t pos = SeekInternal(offset, origin);
		if (pos < 0) {
			return pos;
		}

		if (_readLength > 0) {
			if (oldPos == pos) {
				// Seek after the buffered part, so the position is still correct
				if (SeekInternal(_readLength - _readPos, SeekOrigin::Current) < 0) {
					// This shouldn't fail, but if it does, invalidate the buffer
					_readPos = 0;
					_readLength = 0;
				}
			} else if (oldPos - _readPos <= pos && pos < oldPos + _readLength - _readPos) {
				// Some part of the buffer is still valid
				std::int32_t diff = static_cast<std::int32_t>(pos - oldPos);
				_readPos += diff;
				// Seek after the buffered part, so the position is still correct
				if (SeekInternal(_readLength - _readPos, SeekOrigin::Current) < 0) {
					// This shouldn't fail, but if it does, invalidate the buffer
					_readPos = 0;
					_readLength = 0;
				}
			} else {
				_readPos = 0;
				_readLength = 0;
			}
		}

		return pos;
	}

	std::int64_t FileStream::GetPosition() const
	{
		return (_filePos - _readLength) + _readPos + _writePos;
	}

	std::int64_t FileStream::Read(void* destination, std::int64_t bytesToRead)
	{
		if DEATH_UNLIKELY(bytesToRead <= 0) {
			return 0;
		}

		DEATH_ASSERT(destination != nullptr, "destination is null", 0);
		std::uint8_t* typedBuffer = static_cast<std::uint8_t*>(destination);

		bool isBlocked = false;
		std::int64_t n = (_readLength - _readPos);
		if (n == 0) {
			if (_writePos > 0) {
				FlushWriteBuffer();
			}
			if (bytesToRead >= _bufferSize) {
				_readPos = 0;
				_readLength = 0;

				do {
					std::int32_t partialBytesToRead = (bytesToRead < INT32_MAX ? (std::int32_t)bytesToRead : INT32_MAX);
					std::int32_t bytesRead = ReadInternal(&typedBuffer[n], partialBytesToRead);
					if DEATH_UNLIKELY(bytesRead < 0) {
						return bytesRead;
					} else if DEATH_UNLIKELY(bytesRead == 0) {
						break;
					}
					n += bytesRead;
					bytesToRead -= bytesRead;
				} while (bytesToRead > 0);

				return n;
			}

			InitializeBuffer();
			n = ReadInternal(&_buffer[0], _bufferSize);
			if (n == 0) {
				return 0;
			}
			isBlocked = (n < _bufferSize);
			_readPos = 0;
			_readLength = (std::int32_t)n;
		}

		if (bytesToRead < n) {
			n = bytesToRead;
		}

		std::memcpy(typedBuffer, &_buffer[_readPos], n);
		_readPos += (std::int32_t)n;

		bytesToRead -= n;
		if (bytesToRead > 0 && !isBlocked) {
			DEATH_DEBUG_ASSERT(_readPos == _readLength);
			_readPos = 0;
			_readLength = 0;

			do {
				std::int32_t partialBytesToRead = (bytesToRead < INT32_MAX ? (std::int32_t)bytesToRead : INT32_MAX);
				std::int32_t bytesRead = ReadInternal(&typedBuffer[n], partialBytesToRead);
				if DEATH_UNLIKELY(bytesRead < 0) {
					return bytesRead;
				} else if DEATH_UNLIKELY(bytesRead == 0) {
					break;
				}
				n += bytesRead;
				bytesToRead -= bytesRead;
			} while (bytesToRead > 0);
		}

		return n;
	}

	std::int64_t FileStream::Write(const void* source, std::int64_t bytesToWrite)
	{
		if DEATH_UNLIKELY(bytesToWrite <= 0) {
			return 0;
		}

		DEATH_ASSERT(source != nullptr, "source is null", 0);
		const std::uint8_t* typedBuffer = static_cast<const std::uint8_t*>(source);
		std::int64_t bytesWrittenTotal = 0;

		if (_writePos == 0) {
			if (_readPos < _readLength) {
				FlushReadBuffer();
			}
			_readPos = 0;
			_readLength = 0;
		}

		if (_writePos > 0) {
			std::int32_t bufferBytesLeft = (_bufferSize - _writePos);
			if (bufferBytesLeft > 0) {
				if (bytesToWrite <= bufferBytesLeft) {
					std::memcpy(&_buffer[_writePos], typedBuffer, bytesToWrite);
					_writePos += (std::int32_t)bytesToWrite;
					return bytesToWrite;
				} else {
					std::memcpy(&_buffer[_writePos], typedBuffer, bufferBytesLeft);
					_writePos += bufferBytesLeft;
					typedBuffer += bufferBytesLeft;
					bytesWrittenTotal += bufferBytesLeft;
					bytesToWrite -= bufferBytesLeft;
				}
			}

			WriteInternal(&_buffer[0], _writePos);
			_writePos = 0;
		}

		if (bytesToWrite >= _bufferSize) {
			while DEATH_UNLIKELY(bytesToWrite > INT32_MAX) {
				std::int32_t moreBytesRead = WriteInternal(typedBuffer, INT32_MAX);
				if DEATH_UNLIKELY(moreBytesRead <= 0) {
					return bytesWrittenTotal;
				}
				typedBuffer += moreBytesRead;
				bytesWrittenTotal += moreBytesRead;
				bytesToWrite -= moreBytesRead;
			}
			if DEATH_LIKELY(bytesToWrite > 0) {
				std::int32_t moreBytesRead = WriteInternal(typedBuffer, (std::int32_t)bytesToWrite);
				bytesWrittenTotal += moreBytesRead;
			}
			return bytesWrittenTotal;
		}

		// Copy remaining bytes into buffer, it will be written to the file later
		InitializeBuffer();
		std::memcpy(&_buffer[_writePos], typedBuffer, bytesToWrite);
		_writePos = (std::int32_t)bytesToWrite;
		bytesWrittenTotal += bytesToWrite;
		return bytesWrittenTotal;
	}

	bool FileStream::Flush()
	{
		if (_writePos > 0) {
			FlushWriteBuffer();
		} else if (_readPos < _readLength) {
			FlushReadBuffer();
		}

#if defined(DEATH_TARGET_WINDOWS)
		return ::FlushFileBuffers(_fileHandle);
#elif defined(_POSIX_SYNCHRONIZED_IO) && _POSIX_SYNCHRONIZED_IO > 0
		return ::fdatasync(_fileDescriptor) == 0;
#else
		return ::fsync(_fileDescriptor) == 0;
#endif
	}

	bool FileStream::IsValid()
	{
#if defined(DEATH_TARGET_WINDOWS)
		return (_fileHandle != INVALID_HANDLE_VALUE);
#else
		return (_fileDescriptor >= 0);
#endif
	}

	std::int64_t FileStream::GetSize() const
	{
		std::int64_t size = _size;
		if DEATH_UNLIKELY(_writePos > 0 && _filePos + _writePos > _size) {
			size = _filePos + _writePos;
		}
		return size;
	}

	Containers::StringView FileStream::GetPath() const
	{
		return _path;
	}

	void FileStream::InitializeBuffer()
	{
		if DEATH_UNLIKELY(_buffer == nullptr) {
			_buffer = std::make_unique<char[]>(_bufferSize);
		}
	}

	void FileStream::FlushReadBuffer()
	{
		std::int64_t rewind = (_readPos - _readLength);
		if (rewind != 0) {
			SeekInternal(rewind, SeekOrigin::Current);
		}
		_readPos = 0;
		_readLength = 0;
	}

	void FileStream::FlushWriteBuffer()
	{
		if (_writePos > 0) {
			WriteInternal(&_buffer[0], _writePos);
			_writePos = 0;
		}
	}

	void FileStream::Open(FileAccess mode)
	{
#if defined(DEATH_TARGET_WINDOWS)
		DWORD desireAccess, creationDisposition, shareMode;
		switch (mode & FileAccess::ReadWrite) {
			case FileAccess::Read:
				desireAccess = GENERIC_READ;
				creationDisposition = OPEN_EXISTING;
				shareMode = ((mode & FileAccess::Exclusive) == FileAccess::Exclusive ? 0 : FILE_SHARE_READ | FILE_SHARE_WRITE);
				break;
			case FileAccess::Write:
				desireAccess = GENERIC_WRITE;
				creationDisposition = CREATE_ALWAYS;
				shareMode = ((mode & FileAccess::Exclusive) == FileAccess::Exclusive ? 0 : FILE_SHARE_READ);
				break;
			case FileAccess::ReadWrite:
				desireAccess = GENERIC_READ | GENERIC_WRITE;
				creationDisposition = /*OPEN_ALWAYS*/OPEN_EXISTING;	// NOTE: File must already exist
				shareMode = ((mode & FileAccess::Exclusive) == FileAccess::Exclusive ? 0 : FILE_SHARE_READ | FILE_SHARE_WRITE);
				break;
			default:
#	if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("Cannot open file \"%s\" because of invalid mode (%u)", _path.data(), (std::uint32_t)mode);
#	endif
				return;
		}

		SECURITY_ATTRIBUTES securityAttribs = { sizeof(SECURITY_ATTRIBUTES) };
		securityAttribs.bInheritHandle = (mode & FileAccess::InheritHandle) == FileAccess::InheritHandle;

#	if defined(DEATH_TARGET_WINDOWS_RT)
		CREATEFILE2_EXTENDED_PARAMETERS params = { sizeof(CREATEFILE2_EXTENDED_PARAMETERS), FILE_ATTRIBUTE_NORMAL };
		if ((mode & FileAccess::Sequential) == FileAccess::Sequential) {
			params.dwFileFlags = FILE_FLAG_SEQUENTIAL_SCAN;
		}
		params.lpSecurityAttributes = &securityAttribs;
		_fileHandle = ::CreateFile2FromAppW(Utf8::ToUtf16(_path), desireAccess, shareMode, creationDisposition, &params);
#	else
		DWORD fileFlags = FILE_ATTRIBUTE_NORMAL;
		if ((mode & FileAccess::Sequential) == FileAccess::Sequential) {
			fileFlags |= FILE_FLAG_SEQUENTIAL_SCAN;
		}
		_fileHandle = ::CreateFile(Utf8::ToUtf16(_path), desireAccess, shareMode, &securityAttribs, creationDisposition, fileFlags, NULL);
#	endif
		if (_fileHandle == INVALID_HANDLE_VALUE) {
			DWORD error = ::GetLastError();
#		if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("Cannot open file \"%s\" with error 0x%08x%s", _path.data(), error, __GetWin32ErrorSuffix(error));
#		endif
			return;
		}

		LARGE_INTEGER fileSize;
		if (::GetFileSizeEx(_fileHandle, &fileSize)) {
			_size = fileSize.QuadPart;
		}
#else
		int openFlags;
		switch (mode & FileAccess::ReadWrite) {
			case FileAccess::Read:
				openFlags = O_RDONLY;
				break;
			case FileAccess::Write:
				openFlags = O_WRONLY | O_CREAT | O_TRUNC;
				break;
			case FileAccess::ReadWrite:
				openFlags = O_RDWR;	// NOTE: File must already exist
				break;
			default:
#	if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("Cannot open file \"%s\" because of invalid mode (%u)", _path.data(), (std::uint32_t)mode);
#	endif
				return;
		}
		if ((mode & FileAccess::InheritHandle) != FileAccess::InheritHandle) {
			openFlags |= O_CLOEXEC;
		}

		int defaultPermissions = (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH); // 0666
		_fileDescriptor = ::open(_path.data(), openFlags, defaultPermissions);
		if (_fileDescriptor < 0) {
#	if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("Cannot open file \"%s\" with error %i%s", _path.data(), errno, __GetUnixErrorSuffix(errno));
#	endif
			return;
		}

#	if defined(POSIX_FADV_SEQUENTIAL) && (!defined(__ANDROID__) || __ANDROID_API__ >= 21) && !defined(DEATH_TARGET_SWITCH)
		if ((mode & FileAccess::Sequential) == FileAccess::Sequential) {
			// As noted in https://eklitzke.org/efficient-file-copying-on-linux, might make the file reading faster
			::posix_fadvise(_fileDescriptor, 0, 0, POSIX_FADV_SEQUENTIAL);
		}
#	endif

		struct stat sb;
		if (::fstat(_fileDescriptor, &sb) == 0 && S_ISREG(sb.st_mode)) {
			_size = std::int64_t(sb.st_size);
		}
#endif

#if defined(DEATH_TRACE_VERBOSE_IO)
		switch (mode & FileAccess::ReadWrite) {
			default: LOGB("File \"%s\" opened", _path.data()); break;
			case FileAccess::Write: LOGB("File \"%s\" opened for write", _path.data()); break;
			case FileAccess::ReadWrite: LOGB("File \"%s\" opened for read+write", _path.data()); break;
		}
#endif
	}

	std::int64_t FileStream::SeekInternal(std::int64_t offset, SeekOrigin origin)
	{
#if defined(DEATH_TARGET_WINDOWS)
		LARGE_INTEGER distanceToMove;
		distanceToMove.QuadPart = offset;

		LARGE_INTEGER newPos;
		if (!::SetFilePointerEx(_fileHandle, distanceToMove, &newPos, (DWORD)origin)) {
			DWORD error = ::GetLastError();
			if (error != ERROR_BROKEN_PIPE) {
#	if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("Cannot change position in file \"%s\" with error 0x%08x%s", _path.data(), error, __GetWin32ErrorSuffix(error));
#	endif
			}
			return Stream::OutOfRange;
		}

		_filePos = newPos.QuadPart;
		return newPos.QuadPart;
#else
		std::int64_t newPos = ::lseek(_fileDescriptor, offset, static_cast<std::int32_t>(origin));
		if (newPos == -1) {
#	if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("Cannot change position in file \"%s\" with error %i%s", _path.data(), errno, __GetUnixErrorSuffix(errno));
#	endif
			return Stream::OutOfRange;
		}
		_filePos = newPos;
		return newPos;
#endif
	}

	std::int32_t FileStream::ReadInternal(void* destination, std::int32_t bytesToRead)
	{
#if defined(DEATH_TARGET_WINDOWS)
		DWORD bytesRead;
		if (!::ReadFile(_fileHandle, destination, bytesToRead, &bytesRead, NULL)) {
			DWORD error = ::GetLastError();
			if (error != ERROR_BROKEN_PIPE) {
#	if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("Cannot read from file \"%s\" with error 0x%08x%s", _path.data(), error, __GetWin32ErrorSuffix(error));
#	endif
			}
			return -1;
		}

		_filePos += static_cast<std::int32_t>(bytesRead);
		return static_cast<std::int32_t>(bytesRead);
#else
		std::int32_t bytesRead = static_cast<std::int32_t>(::read(_fileDescriptor, destination, bytesToRead));
		if (bytesRead < 0) {
#	if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("Cannot read from file \"%s\" with error %i%s", _path.data(), errno, __GetUnixErrorSuffix(errno));
#	endif
			return -1;
		}
		_filePos += bytesRead;
		return bytesRead;
#endif
	}

	std::int32_t FileStream::WriteInternal(const void* source, std::int32_t bytesToWrite)
	{
#if defined(DEATH_TARGET_WINDOWS)
		DWORD bytesWritten;
		if (!::WriteFile(_fileHandle, source, bytesToWrite, &bytesWritten, NULL)) {
			DWORD error = ::GetLastError();
			if (error != ERROR_NO_DATA) {
#	if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("Cannot write to file \"%s\" with error 0x%08x%s", _path.data(), error, __GetWin32ErrorSuffix(error));
#	endif
			}
			return -1;
		}

		_filePos += static_cast<std::int32_t>(bytesWritten);
		return static_cast<std::int32_t>(bytesWritten);
#else
		std::int32_t bytesWritten = static_cast<std::int32_t>(::write(_fileDescriptor, source, bytesToWrite));
		if (bytesWritten < 0) {
#	if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("Cannot write to file \"%s\" with error %i%s", _path.data(), errno, __GetUnixErrorSuffix(errno));
#	endif
			return -1;
		}
		_filePos += bytesWritten;
		return bytesWritten;
#endif
	}

}}
