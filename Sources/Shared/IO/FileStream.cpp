#include "FileStream.h"

#include "../CommonWindows.h"
#include "../Asserts.h"
#include "../Utf8.h"
#include "../Containers/Array.h"
#include "../Containers/SmallVector.h"

#include <cstring>

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_MINGW)
#	include <io.h>
#	if defined(DEATH_TARGET_WINDOWS_RT)
#		include <fcntl.h>
#	endif
#else
#	include <cerrno>
#	include <fcntl.h>
#	include <sys/stat.h>
#	include <unistd.h>
#	if !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_PSP) && !defined(DEATH_TARGET_VITA)
#		include <sys/file.h>	// For flock()
#	endif
#endif

using namespace Death::Containers;

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	namespace
	{
#if defined(DEATH_TARGET_DREAMCAST)
		// The Dreamcast's storage drivers transfer straight into memory only when the destination is
		// 32-byte aligned and otherwise fall back to a copy loop that is orders of magnitude slower, so
		// reads with an unsuitable destination are routed through the stream's own aligned buffer
		constexpr std::uintptr_t DmaAlignment = 32;

		DEATH_ALWAYS_INLINE bool IsDmaFriendly(const void* ptr)
		{
			return ((std::uintptr_t(ptr) & (DmaAlignment - 1)) == 0);
		}
#else
		// Every other platform copies through the kernel, so any destination performs the same
		DEATH_ALWAYS_INLINE bool IsDmaFriendly(const void*)
		{
			return true;
		}
#endif
	}

#if defined(DEATH_TARGET_WINDOWS)
	const char* __GetWin32ErrorSuffix(DWORD error)
	{
		switch (error) {
			case ERROR_FILE_NOT_FOUND: return " (FILE_NOT_FOUND)"; break;
			case ERROR_PATH_NOT_FOUND: return " (PATH_NOT_FOUND)"; break;
			case ERROR_ACCESS_DENIED: return " (ACCESS_DENIED)"; break;
			case ERROR_INVALID_HANDLE: return " (INVALID_HANDLE)"; break;
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
			case EAGAIN: return " (Resource temporarily unavailable)"; break;
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

	FileStream::FileStream(StringView path, FileAccess mode, std::int32_t bufferSize)
		: FileStream(String{path}, mode, bufferSize)
	{
	}

	FileStream::FileStream(String&& path, FileAccess mode, std::int32_t bufferSize)
		: _path(Death::move(path)), _size(Stream::Invalid), _filePos(0),
#if defined(DEATH_TARGET_WINDOWS)
			_fileHandle(INVALID_HANDLE_VALUE),
#else
			_fileDescriptor(-1),
#endif
			_readPos(0), _readLength(0), _writePos(0),
			// Anything below one byte can't be buffered, such a size is treated as a request for unbuffered access
			_bufferSize(bufferSize > 0 ? bufferSize : 0)
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
		_readPos = 0;
		_readLength = 0;
		_writePos = 0;

#if defined(DEATH_TARGET_WINDOWS)
		if (_fileHandle != INVALID_HANDLE_VALUE) {
			void* fileHandle = _fileHandle;
			_fileHandle = INVALID_HANDLE_VALUE;
			if (::CloseHandle(fileHandle)) {
#	if defined(DEATH_TRACE_VERBOSE_IO)
				LOGB("File \"{}\" closed", _path);
#	endif
			} else {
#	if defined(DEATH_TRACE_VERBOSE_IO)
				LOGW("Failed to close file \"{}\"", _path);
#	endif
			}
		}
#else
		if (_fileDescriptor >= 0) {
			std::int32_t fileDescriptor = _fileDescriptor;
			_fileDescriptor = -1;
			if (::close(fileDescriptor) >= 0) {
#	if defined(DEATH_TRACE_VERBOSE_IO)
				LOGB("File \"{}\" closed", _path);
#	endif
			} else {
#	if defined(DEATH_TRACE_VERBOSE_IO)
				LOGW("Failed to close file \"{}\"", _path);
#	endif
			}
		}
#endif

		_buffer = nullptr;
#if defined(DEATH_TARGET_DREAMCAST)
		_bufferAligned = nullptr;
#endif
	}

	std::int64_t FileStream::Seek(std::int64_t offset, SeekOrigin origin)
	{
		if (_writePos > 0) {
			if DEATH_UNLIKELY(!FlushWriteBuffer()) {
				// Bytes left in the write buffer belong to the current position, seeking away would flush
				// them somewhere else entirely
				return Stream::Invalid;
			}
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
				std::int32_t diff = std::int32_t(pos - oldPos);
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
				if DEATH_UNLIKELY(!FlushWriteBuffer()) {
					// Bytes left in the write buffer belong to the current position, reading past them would
					// return the content they are about to replace
					return Stream::Invalid;
				}
			}
			// A read at least as large as the buffer normally goes straight to the caller's memory, but
			// some storage drivers only use their fast path for suitably aligned destinations - those fall
			// back to the buffered path below, which copies through the aligned buffer (if there is one)
			if (bytesToRead >= _bufferSize && (_bufferSize <= 0 || IsDmaFriendly(typedBuffer))) {
				_readPos = 0;
				_readLength = 0;

				do {
					std::int32_t partialBytesToRead = (bytesToRead < INT32_MAX ? std::int32_t(bytesToRead) : INT32_MAX);
					std::int32_t bytesRead = ReadInternal(&typedBuffer[n], partialBytesToRead);
					if DEATH_UNLIKELY(bytesRead <= 0) {
						// Bytes already delivered to the caller have to be reported, otherwise they would be
						// lost - the failure (if any) is reported again by the next call
						return (n > 0 ? n : bytesRead);
					}
					n += bytesRead;
					bytesToRead -= bytesRead;
				} while (bytesToRead > 0);

				return n;
			}

			InitializeBuffer();
			n = ReadInternal(BufferForIo(), _bufferSize);
			if DEATH_UNLIKELY(n <= 0) {
				return n;
			}
			isBlocked = (n < _bufferSize);
			_readPos = 0;
			_readLength = (std::int32_t)n;
		}

		if (bytesToRead < n) {
			n = bytesToRead;
		}

		std::memcpy(typedBuffer, &BufferForIo()[_readPos], std::size_t(n));
		_readPos += std::int32_t(n);

		bytesToRead -= n;
		if (bytesToRead > 0 && !isBlocked) {
			DEATH_DEBUG_ASSERT(_readPos == _readLength);
			_readPos = 0;
			_readLength = 0;

			while (bytesToRead > 0) {
				std::int32_t bytesRead;
				if (_bufferSize <= 0 || IsDmaFriendly(&typedBuffer[n])) {
					std::int32_t partialBytesToRead = (bytesToRead < INT32_MAX ? std::int32_t(bytesToRead) : INT32_MAX);
					bytesRead = ReadInternal(&typedBuffer[n], partialBytesToRead);
				} else {
					// Unsuitable destination: read into the aligned buffer and copy across
					InitializeBuffer();
					std::int32_t partialBytesToRead = (bytesToRead < _bufferSize ? std::int32_t(bytesToRead) : _bufferSize);
#if defined(DEATH_TARGET_DREAMCAST)
					// Bounce only up to the next aligned destination address: the buffer size is a multiple
					// of the alignment, so a full-buffer bounce would keep the destination misaligned - and
					// every remaining chunk on this slow path - for the whole rest of the read
					const std::int32_t alignPrefix = std::int32_t(DmaAlignment - (std::uintptr_t(&typedBuffer[n]) & (DmaAlignment - 1)));
					if (partialBytesToRead > alignPrefix) {
						partialBytesToRead = alignPrefix;
					}
#endif
					bytesRead = ReadInternal(BufferForIo(), partialBytesToRead);
					if (bytesRead > 0) {
						std::memcpy(&typedBuffer[n], BufferForIo(), std::size_t(bytesRead));
					}
				}
				if DEATH_UNLIKELY(bytesRead <= 0) {
					// Bytes already delivered to the caller have to be reported, otherwise they would be
					// lost - the failure (if any) is reported again by the next call
					break;
				}
				n += bytesRead;
				bytesToRead -= bytesRead;
			}
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
					std::memcpy(&BufferForIo()[_writePos], typedBuffer, std::size_t(bytesToWrite));
					_writePos += std::int32_t(bytesToWrite);
					return bytesToWrite;
				} else {
					std::memcpy(&BufferForIo()[_writePos], typedBuffer, bufferBytesLeft);
					_writePos += bufferBytesLeft;
					typedBuffer += bufferBytesLeft;
					bytesWrittenTotal += bufferBytesLeft;
					bytesToWrite -= bufferBytesLeft;
				}
			}

			if DEATH_UNLIKELY(!FlushWriteBuffer()) {
				// What couldn't be written is still held by the buffer, so it's counted as accepted and
				// a subsequent flush can retry it, but nothing more can be appended after it
				return (bytesWrittenTotal > 0 ? bytesWrittenTotal : std::int64_t(Stream::Invalid));
			}
		}

		if (bytesToWrite >= _bufferSize) {
			while (bytesToWrite > 0) {
				std::int32_t partialBytesToWrite = (bytesToWrite < INT32_MAX ? std::int32_t(bytesToWrite) : INT32_MAX);
				std::int32_t bytesWritten = WriteInternal(typedBuffer, partialBytesToWrite);
				if DEATH_UNLIKELY(bytesWritten <= 0) {
					// Bytes already accepted by the file have to be reported, otherwise the caller would
					// write them twice - the failure (if any) is reported again by the next call
					return (bytesWrittenTotal > 0 ? bytesWrittenTotal : std::int64_t(bytesWritten));
				}
				typedBuffer += bytesWritten;
				bytesWrittenTotal += bytesWritten;
				bytesToWrite -= bytesWritten;
			}
			return bytesWrittenTotal;
		}

		// Copy remaining bytes into buffer, it will be written to the file later
		InitializeBuffer();
		std::memcpy(&BufferForIo()[_writePos], typedBuffer, std::size_t(bytesToWrite));
		_writePos = std::int32_t(bytesToWrite);
		bytesWrittenTotal += bytesToWrite;
		return bytesWrittenTotal;
	}

	bool FileStream::Flush()
	{
		bool result = true;
		if (_writePos > 0) {
			result = FlushWriteBuffer();
		} else if (_readPos < _readLength) {
			FlushReadBuffer();
		}

#if defined(DEATH_TARGET_WINDOWS)
		if (!::FlushFileBuffers(_fileHandle) && ::GetLastError() != ERROR_ACCESS_DENIED) {
			// A handle opened for reading only has nothing to synchronize, and that's the sole reason it can
			// be denied here - ignoring it keeps the result consistent with Unix, where fsync() on such
			// a file descriptor succeeds
			result = false;
		}
		return result;
#elif defined(_POSIX_SYNCHRONIZED_IO) && _POSIX_SYNCHRONIZED_IO > 0
		return (::fdatasync(_fileDescriptor) == 0 && result);
#elif defined(DEATH_TARGET_DREAMCAST)
		// fsync() is not implemented in KOS
		return result;
#else
		return (::fsync(_fileDescriptor) == 0 && result);
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
		if DEATH_UNLIKELY(_size < 0) {
			// The size is unknown (e.g., the file is not a regular file), buffered bytes can't make it known
			return _size;
		}

		std::int64_t size = _size;
		if DEATH_UNLIKELY(_writePos > 0 && _filePos + _writePos > size) {
			size = _filePos + _writePos;
		}
		return size;
	}

	std::int64_t FileStream::SetSize(std::int64_t size)
	{
		// Buffered bytes have to reach the file before it's resized, otherwise a pending write would be
		// flushed only afterwards and grow the file again
		if (_writePos > 0) {
			if DEATH_UNLIKELY(!FlushWriteBuffer()) {
				return Stream::Invalid;
			}
		} else if (_readPos < _readLength) {
			FlushReadBuffer();
		}

#if defined(DEATH_TARGET_WINDOWS)
		LARGE_INTEGER liSize;
		liSize.QuadPart = size;

		FILE_END_OF_FILE_INFO eofInfo = { liSize };
		if (!::SetFileInformationByHandle(_fileHandle, FileEndOfFileInfo, &eofInfo, sizeof(eofInfo))) {
			DWORD error = ::GetLastError();
#	if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("Failed to resize file \"{}\" with error 0x{:.8x}{}", _path, error, __GetWin32ErrorSuffix(error));
			// Tracing above clobbers the thread's last error, restore it for callers that inspect it directly
			::SetLastError(error);
#	endif
			return (error == ERROR_INVALID_PARAMETER ? Stream::OutOfRange : Stream::Invalid);
		}
#elif !defined(DEATH_TARGET_PSP) && !defined(DEATH_TARGET_VITA) && !defined(DEATH_TARGET_DREAMCAST) // TODO: ftruncate() is not defined on PSPSDK, VITA and KOS
		if (::ftruncate(_fileDescriptor, size) < 0) {
			std::int32_t error = errno;
#	if defined(DEATH_TRACE_VERBOSE_IO)
			LOGE("Failed to resize file \"{}\" with error {}{}", _path, error, __GetUnixErrorSuffix(error));
			// Tracing above can clobber errno, restore it for callers that inspect it directly
			errno = error;
#	endif
			return (error == EINVAL ? Stream::OutOfRange : Stream::Invalid);
		}
#else
		// The file size cannot be changed on this platform
		return Stream::Invalid;
#endif

		// Neither of the calls above moves the file pointer, so it has to be clamped explicitly - otherwise
		// GetPosition() would report a position that is past the new end of file
		if (_filePos > size) {
			SeekInternal(size, SeekOrigin::Begin);
		}

		_size = size;
		_readPos = 0;
		_readLength = 0;
		_writePos = 0;
		return size;
	}

	StringView FileStream::GetPath() const
	{
		return _path;
	}

	void FileStream::InitializeBuffer()
	{
		if DEATH_UNLIKELY(_buffer == nullptr) {
#if defined(DEATH_TARGET_DREAMCAST)
			// The buffer is the destination of every read, so it has to satisfy the alignment the storage
			// driver needs for its fast path (see IsDmaFriendly), the padding leaves room to align it
			_buffer = std::make_unique<char[]>(_bufferSize + DmaAlignment);
			const std::uintptr_t address = std::uintptr_t(_buffer.get());
			_bufferAligned = _buffer.get() + ((DmaAlignment - (address & (DmaAlignment - 1))) & (DmaAlignment - 1));
#else
			_buffer = std::make_unique<char[]>(_bufferSize);
#endif
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

	bool FileStream::FlushWriteBuffer()
	{
		if (_writePos <= 0) {
			return true;
		}

		std::int32_t bytesWritten = WriteInternal(BufferForIo(), _writePos);
		if DEATH_UNLIKELY(bytesWritten < _writePos) {
			// Whatever couldn't be written is moved to the front of the buffer, so a subsequent flush can
			// retry it instead of dropping it silently
			if (bytesWritten > 0) {
				std::memmove(BufferForIo(), &BufferForIo()[bytesWritten], std::size_t(_writePos - bytesWritten));
				_writePos -= bytesWritten;
			}
			return false;
		}

		_writePos = 0;
		return true;
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
				LOGE("Failed to open file \"{}\" because of invalid mode ({})", _path, std::uint32_t(mode));
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

		SmallVector<wchar_t, MAX_PATH> pathW(DefaultInit, _path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), _path.data(), std::int32_t(_path.size()));
		_fileHandle = ::CreateFile2FromAppW(pathW.data(), desireAccess, shareMode, creationDisposition, &params);
#	else
		DWORD fileFlags = FILE_ATTRIBUTE_NORMAL;
		if ((mode & FileAccess::Sequential) == FileAccess::Sequential) {
			fileFlags |= FILE_FLAG_SEQUENTIAL_SCAN;
		}

		SmallVector<wchar_t, MAX_PATH + 1> pathW(DefaultInit, _path.size() + 1);
		Utf8::ToUtf16(pathW.data(), std::int32_t(pathW.size()), _path.data(), std::int32_t(_path.size()));
		_fileHandle = ::CreateFile(pathW.data(), desireAccess, shareMode, &securityAttribs, creationDisposition, fileFlags, NULL);
#	endif
		if (_fileHandle == INVALID_HANDLE_VALUE) {
#		if defined(DEATH_TRACE_VERBOSE_IO)
			DWORD error = ::GetLastError();
			LOGE("Failed to open file \"{}\" with error 0x{:.8x}{}", _path, error, __GetWin32ErrorSuffix(error));
			// Tracing above clobbers the thread's last error, restore it for callers that inspect it directly
			::SetLastError(error);
#		endif
			return;
		}

		// The size stays unknown for anything that is not a file on a disk, which is also what tells
		// the transfer functions not to reissue a partial request (see IsRegularFile())
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
				LOGE("Failed to open file \"{}\" because of invalid mode ({})", _path, std::uint32_t(mode));
#	endif
				return;
		}
#	if !defined(DEATH_TARGET_VITA) && !defined(DEATH_TARGET_PS3)
		// The PS3's newlib declares no O_CLOEXEC, and the flag would have nothing to do there anyway:
		// lv2 has no exec, so a descriptor cannot outlive this process into another one
		if ((mode & FileAccess::InheritHandle) != FileAccess::InheritHandle) {
			openFlags |= O_CLOEXEC;
		}
#	endif

		int defaultPermissions = (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH); // 0666
#	if defined(DEATH_TARGET_PS2)
		// No EINTR retry: the PS2 has no signals, and its newlib port negates the IOP's error into `errno`,
		// so MCMAN's -4 ("no such file") arrives as EINTR (4) and the loop below would spin forever
		_fileDescriptor = ::open(_path.data(), openFlags, defaultPermissions);
#	else
		do {
			_fileDescriptor = ::open(_path.data(), openFlags, defaultPermissions);
		} while (_fileDescriptor < 0 && errno == EINTR);
#	endif
		if (_fileDescriptor < 0) {
#	if defined(DEATH_TRACE_VERBOSE_IO)
			std::int32_t error = errno;
			LOGE("Failed to open file \"{}\" with error {}{}", _path, error, __GetUnixErrorSuffix(error));
			// Tracing above can clobber errno, restore it for callers that inspect it directly
			errno = error;
#	endif
			return;
		}

#	if !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_PS2) && !defined(DEATH_TARGET_PSP) && \
		!defined(DEATH_TARGET_VITA) && !defined(DEATH_TARGET_WII) && !defined(DEATH_TARGET_GAMECUBE) && \
		!defined(DEATH_TARGET_DREAMCAST) && !defined(DEATH_TARGET_PS3)
		if ((mode & FileAccess::Exclusive) == FileAccess::Exclusive) {
			// Windows opens exclusive files with a share mode of 0, denying any other opener. Modern Linux has no
			// usable mandatory locking, so emulate it with an advisory whole-file lock bound to the open file
			// description (released automatically on close). LOCK_NB fails immediately - similar to
			// ERROR_SHARING_VIOLATION on Windows - instead of blocking when another process already holds the lock.
			if (::flock(_fileDescriptor, LOCK_EX | LOCK_NB) < 0) {
				std::int32_t error = errno;
#		if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("Failed to exclusively lock file \"{}\" with error {}{}", _path, error, __GetUnixErrorSuffix(error));
#		endif
				::close(_fileDescriptor);
				_fileDescriptor = -1;
				// Both the tracing above and close() clobber errno, restore what actually failed here
				errno = error;
				return;
			}
		}
#	endif

#	if defined(POSIX_FADV_SEQUENTIAL) && (!defined(__ANDROID__) || __ANDROID_API__ >= 21) && !defined(DEATH_TARGET_SWITCH)
		if ((mode & FileAccess::Sequential) == FileAccess::Sequential) {
			// As noted in https://eklitzke.org/efficient-file-copying-on-linux, might make the file reading faster
			::posix_fadvise(_fileDescriptor, 0, 0, POSIX_FADV_SEQUENTIAL);
		}
#	endif

		// The size stays unknown for anything that is not a regular file (a pipe, a socket, a terminal), which
		// is also what tells the transfer functions not to reissue a partial request (see IsRegularFile())
		struct stat sb;
		if (::fstat(_fileDescriptor, &sb) == 0 && S_ISREG(sb.st_mode)) {
			_size = std::int64_t(sb.st_size);
		}
#	if defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE) || defined(DEATH_TARGET_DREAMCAST)
		else {
			// fstat() is not fully supported by some libfat/KOS filesystems (e.g., iso9660), measure the size
			// by seeking instead - a successful seek also proves the file behaves like a regular one
			off_t seekEnd = ::lseek(_fileDescriptor, 0, SEEK_END);
			if (seekEnd >= 0) {
				_size = std::int64_t(seekEnd);
				::lseek(_fileDescriptor, 0, SEEK_SET);
			}
		}
#	endif
#endif

#if defined(DEATH_TRACE_VERBOSE_IO)
		switch (mode & FileAccess::ReadWrite) {
			default: LOGB("File \"{}\" opened", _path); break;
			case FileAccess::Write: LOGB("File \"{}\" opened for write", _path); break;
			case FileAccess::ReadWrite: LOGB("File \"{}\" opened for read+write", _path); break;
		}
#endif
	}

	std::int64_t FileStream::SeekInternal(std::int64_t offset, SeekOrigin origin)
	{
#if defined(DEATH_TARGET_WINDOWS)
		LARGE_INTEGER distanceToMove;
		distanceToMove.QuadPart = offset;

		LARGE_INTEGER newPos;
		if (!::SetFilePointerEx(_fileHandle, distanceToMove, &newPos, DWORD(origin))) {
			DWORD error = ::GetLastError();
			if (error != ERROR_BROKEN_PIPE) {
#	if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("Failed to change position in file \"{}\" with error 0x{:.8x}{}", _path, error, __GetWin32ErrorSuffix(error));
				// Tracing above clobbers the thread's last error, restore it for callers that inspect it directly
				::SetLastError(error);
#	endif
			}
			return Stream::OutOfRange;
		}

		_filePos = newPos.QuadPart;
		return newPos.QuadPart;
#else
		std::int64_t newPos = ::lseek(_fileDescriptor, offset, std::int32_t(origin));
		if (newPos < 0) {
#	if defined(DEATH_TRACE_VERBOSE_IO)
			std::int32_t error = errno;
			LOGE("Failed to change position in file \"{}\" with error {}{}", _path, error, __GetUnixErrorSuffix(error));
			// Tracing above can clobber errno, restore it for callers that inspect it directly
			errno = error;
#	endif
			return Stream::OutOfRange;
		}
		_filePos = newPos;
		return newPos;
#endif
	}

	std::int32_t FileStream::ReadInternal(void* destination, std::int32_t bytesToRead)
	{
		// A partial transfer means end of file only for a regular file. Everything else (a pipe, a socket,
		// a terminal) has to return what is available right now, because waiting for the rest of the request
		// would block until the other side happens to produce it. Regular files, on the other hand, can come
		// up short for reasons that have nothing to do with the end of file (a signal delivered mid-transfer,
		// a network/FUSE filesystem, or libfat/KOS returning at a cluster boundary) and the buffered caller
		// would take any of those for end of data, so the request is reissued until it's satisfied
		std::int32_t bytesRead = 0;
		bool failed = false;
		while (bytesRead < bytesToRead) {
#if defined(DEATH_TARGET_WINDOWS)
			DWORD partialRead;
			if DEATH_UNLIKELY(!::ReadFile(_fileHandle, static_cast<std::uint8_t*>(destination) + bytesRead, DWORD(bytesToRead - bytesRead), &partialRead, NULL)) {
#	if defined(DEATH_TRACE_VERBOSE_IO)
				DWORD error = ::GetLastError();
				if (error != ERROR_BROKEN_PIPE) {
					LOGE("Failed to read from file \"{}\" with error 0x{:.8x}{}", _path, error, __GetWin32ErrorSuffix(error));
					// Tracing above clobbers the thread's last error, restore it for callers that inspect it directly
					::SetLastError(error);
				}
#	endif
				failed = true;
				break;
			}
			if DEATH_UNLIKELY(partialRead == 0) {
				break;
			}
			bytesRead += std::int32_t(partialRead);
#else
			std::int32_t partialRead = std::int32_t(::read(_fileDescriptor, static_cast<std::uint8_t*>(destination) + bytesRead, std::size_t(bytesToRead - bytesRead)));
			if DEATH_UNLIKELY(partialRead < 0) {
				if DEATH_UNLIKELY(errno == EINTR) {
					// A signal arrived before anything was transferred, the request can simply be reissued
					continue;
				}
#	if defined(DEATH_TRACE_VERBOSE_IO)
				std::int32_t error = errno;
				LOGE("Failed to read from file \"{}\" with error {}{}", _path, error, __GetUnixErrorSuffix(error));
				// Tracing above can clobber errno, restore it for callers that inspect it directly
				errno = error;
#	endif
				failed = true;
				break;
			}
			if DEATH_UNLIKELY(partialRead == 0) {
				break;
			}
			bytesRead += partialRead;
#endif
			if DEATH_UNLIKELY(!IsRegularFile()) {
				break;
			}
		}

		// Earlier iterations already advanced the kernel offset, so it has to be accounted for even on
		// failure - GetPosition() would otherwise be wrong for the rest of the stream's life
		_filePos += bytesRead;
		// Bytes that were already transferred have to be reported, the failure shows up again on the next call
		return (failed && bytesRead == 0 ? -1 : bytesRead);
	}

	std::int32_t FileStream::WriteInternal(const void* source, std::int32_t bytesToWrite)
	{
		// The request is reissued for the same reasons as in ReadInternal() - a signal delivered mid-transfer
		// or a filesystem that accepts less than asked for would otherwise silently truncate the data
		std::int32_t bytesWritten = 0;
		bool failed = false;
		while (bytesWritten < bytesToWrite) {
#if defined(DEATH_TARGET_WINDOWS)
			DWORD partialRead;
			if DEATH_UNLIKELY(!::WriteFile(_fileHandle, static_cast<const std::uint8_t*>(source) + bytesWritten, DWORD(bytesToWrite - bytesWritten), &partialRead, NULL)) {
#	if defined(DEATH_TRACE_VERBOSE_IO)
				DWORD error = ::GetLastError();
				if (error != ERROR_NO_DATA) {
					LOGE("Failed to write to file \"{}\" with error 0x{:.8x}{}", _path, error, __GetWin32ErrorSuffix(error));
					// Tracing above clobbers the thread's last error, restore it for callers that inspect it directly
					::SetLastError(error);
				}
#	endif
				failed = true;
				break;
			}
			if DEATH_UNLIKELY(partialRead == 0) {
				break;
			}
			bytesWritten += std::int32_t(partialRead);
#else
			std::int32_t partialRead = std::int32_t(::write(_fileDescriptor, static_cast<const std::uint8_t*>(source) + bytesWritten, std::size_t(bytesToWrite - bytesWritten)));
			if DEATH_UNLIKELY(partialRead < 0) {
				if (errno == EINTR) {
					// A signal arrived before anything was transferred, the request can simply be reissued
					continue;
				}
#	if defined(DEATH_TRACE_VERBOSE_IO)
				std::int32_t error = errno;
				LOGE("Failed to write to file \"{}\" with error {}{}", _path, error, __GetUnixErrorSuffix(error));
				// Tracing above can clobber errno, restore it for callers that inspect it directly
				errno = error;
#	endif
				failed = true;
				break;
			}
			if DEATH_UNLIKELY(partialRead == 0) {
				break;
			}
			bytesWritten += partialRead;
#endif
			if DEATH_UNLIKELY(!IsRegularFile()) {
				break;
			}
		}

		// Earlier iterations already advanced the kernel offset, so it has to be accounted for even on
		// failure - GetPosition() would otherwise be wrong for the rest of the stream's life
		_filePos += bytesWritten;
		if (_size >= 0 && _filePos > _size) {
			// The file just grew, otherwise GetSize() would keep reporting the size measured when it was opened
			_size = _filePos;
		}
		// Bytes that were already transferred have to be reported, the failure shows up again on the next call
		return (failed && bytesWritten == 0 ? -1 : bytesWritten);
	}

}}
