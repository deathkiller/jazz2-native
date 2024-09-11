#include "FileSystem.h"
#include "FileStream.h"
#include "MemoryStream.h"
#include "../CommonWindows.h"
#include "../Asserts.h"
#include "../Environment.h"
#include "../Utf8.h"
#include "../Containers/GrowableArray.h"
#include "../Containers/StringConcatenable.h"

#if defined(DEATH_TARGET_WINDOWS)
#	include <fileapi.h>
#	include <shellapi.h>
#	include <shlobj.h>
#	include <timezoneapi.h>
#else
#	include <cerrno>
#	include <cstdio>
#	include <cstring>
#	include <ctime>
#	include <unistd.h>
#	include <sys/stat.h>
#	include <libgen.h>
#	include <pwd.h>
#	include <dirent.h>
#	include <fcntl.h>
#	include <ftw.h>
#
#	if defined(DEATH_TARGET_UNIX)
#		include <sys/mman.h>
#		include <sys/wait.h>
#	endif
#	if defined(DEATH_TARGET_APPLE)
#		include <copyfile.h>
#	elif defined(__linux__)
#		include <sys/sendfile.h>
#	endif
#	if defined(__FreeBSD__)
#		include <sys/types.h>
#		include <sys/sysctl.h>
#	endif
#endif

#if defined(DEATH_TARGET_ANDROID)
#	include "AndroidAssetStream.h"
#elif defined(DEATH_TARGET_APPLE)
#	include <objc/objc-runtime.h>
#	include <mach-o/dyld.h>
#elif defined(DEATH_TARGET_EMSCRIPTEN)
#	include <emscripten/emscripten.h>
#elif defined(DEATH_TARGET_SWITCH)
#	include <alloca.h>
#elif defined(DEATH_TARGET_WINDOWS_RT)
#	include <winrt/Windows.Foundation.h>
#	include <winrt/Windows.Storage.h>
#	include <winrt/Windows.System.h>
#endif

using namespace Death::Containers;
using namespace Death::Containers::Literals;

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

#if defined(DEATH_TARGET_WINDOWS)
	const char* __GetWin32ErrorSuffix(DWORD error);
#endif

	namespace
	{
		static std::size_t GetPathRootLength(const StringView path)
		{
			if (path.empty()) return 0;

#if defined(DEATH_TARGET_WINDOWS)
			constexpr StringView ExtendedPathPrefix = "\\\\?\\"_s;
			constexpr StringView UncExtendedPathPrefix = "\\\\?\\UNC\\"_s;

			std::size_t i = 0;
			std::size_t pathLength = path.size();
			std::size_t volumeSeparatorLength = 2;		// Length to the colon "C:"
			std::size_t uncRootLength = 2;				// Length to the start of the server name "\\"

			bool extendedSyntax = path.hasPrefix(ExtendedPathPrefix);
			bool extendedUncSyntax = path.hasPrefix(UncExtendedPathPrefix);
			if (extendedSyntax) {
				// Shift the position we look for the root from to account for the extended prefix
				if (extendedUncSyntax) {
					// "\\" -> "\\?\UNC\"
					uncRootLength = UncExtendedPathPrefix.size();
				} else {
					// "C:" -> "\\?\C:"
					volumeSeparatorLength += ExtendedPathPrefix.size();
				}
			}

			if ((!extendedSyntax || extendedUncSyntax) && (path[0] == '/' || path[0] == '\\')) {
				// UNC or simple rooted path (e.g., "\foo", NOT "\\?\C:\foo")
				i = 1;
				if (extendedUncSyntax || (pathLength > 1 && (path[1] == '/' || path[1] == '\\'))) {
					// UNC ("\\?\UNC\" or "\\"), scan past the next two directory separators at most (e.g., "\\?\UNC\Server\Share" or "\\Server\Share\")
					i = uncRootLength;
					std::int32_t n = 2;	// Maximum separators to skip
					while (i < pathLength && ((path[i] != '/' && path[i] != '\\') || --n > 0)) {
						i++;
					}
					// Keep the last path separator as part of root prefix
					if (i < pathLength && (path[i] == '/' || path[i] == '\\')) {
						i++;
					}
				}
			} else if (pathLength >= volumeSeparatorLength && path[volumeSeparatorLength - 1] == ':') {
				// Path is at least longer than where we expect a colon and has a colon ("\\?\A:", "A:")
				// If the colon is followed by a directory separator, move past it
				i = volumeSeparatorLength;
				if (pathLength >= volumeSeparatorLength + 1 && (path[volumeSeparatorLength] == '/' || path[volumeSeparatorLength] == '\\')) {
					i++;
				}
			}
			
			return i;
#else
#	if defined(DEATH_TARGET_ANDROID)
			if (path.hasPrefix(AndroidAssetStream::Prefix)) {
				return AndroidAssetStream::Prefix.size();
			}
#	elif defined(DEATH_TARGET_SWITCH)
			constexpr StringView RomfsPrefix = "romfs:/"_s;
			constexpr StringView SdmcPrefix = "sdmc:/"_s;
			if (path.hasPrefix(RomfsPrefix)) {
				return RomfsPrefix.size();
			}
			if (path.hasPrefix(SdmcPrefix)) {
				return SdmcPrefix.size();
			}
#	endif
			return (path[0] == '/' || path[0] == '\\' ? 1 : 0);
#endif
		}

#if defined(DEATH_TARGET_WINDOWS)
		static bool DeleteDirectoryInternal(const ArrayView<wchar_t> path, bool recursive, std::int32_t depth)
		{
			if (recursive) {
				if (path.size() + 3 <= FileSystem::MaxPathLength) {
					auto bufferExtended = Array<wchar_t>(NoInit, FileSystem::MaxPathLength);
					std::memcpy(bufferExtended.data(), path.data(), path.size() * sizeof(wchar_t));

					std::size_t bufferOffset = path.size();
					if (bufferExtended[bufferOffset - 1] == L'/' || path[bufferOffset - 1] == L'\\') {
						bufferExtended[bufferOffset - 1] = L'\\';
					} else {
						bufferExtended[bufferOffset] = L'\\';
						bufferOffset++;
					}

					// Adding a wildcard to list all files in the directory
					bufferExtended[bufferOffset] = L'*';
					bufferExtended[bufferOffset + 1] = L'\0';

					WIN32_FIND_DATA data;
#	if defined(DEATH_TARGET_WINDOWS_RT)
					HANDLE hFindFile = ::FindFirstFileExFromAppW(bufferExtended, FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
#	else
					HANDLE hFindFile = ::FindFirstFileExW(bufferExtended, Environment::IsWindows7() ? FindExInfoBasic : FindExInfoStandard,
						&data, FindExSearchNameMatch, nullptr, Environment::IsWindows7() ? FIND_FIRST_EX_LARGE_FETCH : 0);
#	endif
					if (hFindFile != NULL && hFindFile != INVALID_HANDLE_VALUE) {
						do {
							if (data.cFileName[0] == L'.' && (data.cFileName[1] == L'\0' || (data.cFileName[1] == L'.' && data.cFileName[2] == L'\0'))) {
								continue;
							}

							std::size_t fileNameLength = wcslen(data.cFileName);
							std::memcpy(&bufferExtended[bufferOffset], data.cFileName, fileNameLength * sizeof(wchar_t));

							if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY) {
								// Don't follow symbolic links
								bool shouldRecurse = ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != FILE_ATTRIBUTE_REPARSE_POINT);
								if (shouldRecurse) {
									bufferExtended[bufferOffset + fileNameLength] = L'\0';

									if (depth < 16 && !DeleteDirectoryInternal(bufferExtended.prefix(bufferOffset + fileNameLength), true, depth + 1)) {
										break;
									}
								} else {
									bufferExtended[bufferOffset + fileNameLength] = L'\\';
									bufferExtended[bufferOffset + fileNameLength + 1] = L'\0';

									// Check to see if this is a mount point and unmount it
									if (data.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT) {
										// Use full path plus a trailing '\'
										if (!::DeleteVolumeMountPointW(bufferExtended)) {
											// Cannot unmount this mount point
										}
									}

									// RemoveDirectory() on a symbolic link will remove the link itself
#	if defined(DEATH_TARGET_WINDOWS_RT)
									if (!::RemoveDirectoryFromAppW(bufferExtended)) {
#	else
									if (!::RemoveDirectoryW(bufferExtended)) {
#	endif
										DWORD error = ::GetLastError();
										if (error != ERROR_PATH_NOT_FOUND) {
											// Cannot remove symbolic link
										}
									}
								}
							} else {
								bufferExtended[bufferOffset + fileNameLength] = L'\0';

#	if defined(DEATH_TARGET_WINDOWS_RT)
								::DeleteFileFromAppW(bufferExtended);
#	else
								::DeleteFileW(bufferExtended);
#	endif
							}
						} while (::FindNextFileW(hFindFile, &data));

						::FindClose(hFindFile);
					}
				}
			}

#	if defined(DEATH_TARGET_WINDOWS_RT)
			return ::RemoveDirectoryFromAppW(path);
#	else
			return ::RemoveDirectoryW(path);
#	endif
		}
#else
		static FileSystem::Permission NativeModeToEnum(std::uint32_t nativeMode)
		{
			FileSystem::Permission mode = FileSystem::Permission::None;

			if (nativeMode & S_IRUSR)
				mode |= FileSystem::Permission::Read;
			if (nativeMode & S_IWUSR)
				mode |= FileSystem::Permission::Write;
			if (nativeMode & S_IXUSR)
				mode |= FileSystem::Permission::Execute;

			return mode;
		}

		static std::uint32_t AddPermissionsToCurrent(std::uint32_t currentMode, FileSystem::Permission mode)
		{
			if ((mode & FileSystem::Permission::Read) == FileSystem::Permission::Read)
				currentMode |= S_IRUSR;
			if ((mode & FileSystem::Permission::Write) == FileSystem::Permission::Write)
				currentMode |= S_IWUSR;
			if ((mode & FileSystem::Permission::Execute) == FileSystem::Permission::Execute)
				currentMode |= S_IXUSR;

			return currentMode;
		}

		static std::uint32_t RemovePermissionsFromCurrent(std::uint32_t currentMode, FileSystem::Permission mode)
		{
			if ((mode & FileSystem::Permission::Read) == FileSystem::Permission::Read)
				currentMode &= ~S_IRUSR;
			if ((mode & FileSystem::Permission::Write) == FileSystem::Permission::Write)
				currentMode &= ~S_IWUSR;
			if ((mode & FileSystem::Permission::Execute) == FileSystem::Permission::Execute)
				currentMode &= ~S_IXUSR;

			return currentMode;
		}

#	if !defined(DEATH_TARGET_SWITCH)
		static std::int32_t DeleteDirectoryInternalCallback(const char* fpath, const struct stat* sb, std::int32_t typeflag, struct FTW* ftwbuf)
		{
			return ::remove(fpath);
		}
#	endif

		static bool DeleteDirectoryInternal(const StringView path)
		{
#	if defined(DEATH_TARGET_SWITCH)
			// nftw() is missing in libnx
			auto nullTerminatedPath = String::nullTerminatedView(path);
			DIR* d = ::opendir(nullTerminatedPath.data());
			std::int32_t r = -1;
			if (d != nullptr) {
				r = 0;
				struct dirent* p;
				while (r == 0 && (p = ::readdir(d)) != nullptr) {
					if (strcmp(p->d_name, ".") == 0 || strcmp(p->d_name, "..") == 0) {
						continue;
					}

					String fileName = FileSystem::CombinePath(path, p->d_name);
					struct stat sb;
					if (::lstat(fileName.data(), &sb) == 0) { // Don't follow symbolic links
						if (S_ISDIR(sb.st_mode)) {
							DeleteDirectoryInternal(fileName);
						} else {
							r = ::unlink(fileName.data());
						}
					}
				}
				::closedir(d);
			}

			if (r == 0) {
				r = ::rmdir(nullTerminatedPath.data());
			}
			return (r == 0);
#	else
			// Don't follow symbolic links
			return ::nftw(String::nullTerminatedView(path).data(), DeleteDirectoryInternalCallback, 64, FTW_DEPTH | FTW_PHYS) == 0;
#	endif
		}

#	if defined(DEATH_TARGET_UNIX)
		static bool RedirectFileDescriptorToNull(std::int32_t fd)
		{
			if (fd < 0) {
				return false;
			}

			std::int32_t tempfd;
			do {
				tempfd = ::open("/dev/null", O_RDWR | O_NOCTTY);
			} while (tempfd == -1 && errno == EINTR);

			if (tempfd == -1) {
				return false;
			}

			if (tempfd != fd) {
				if (::dup2(tempfd, fd) == -1) {
					::close(tempfd);
					return false;
				}
				if (::close(tempfd) == -1) {
					return false;
				}
			}

			return true;
		}

		static void TryCloseAllFileDescriptors()
		{
			DIR* d = ::opendir("/proc/self/fd/");
			if (d == nullptr) {
				const long fd_max = ::sysconf(_SC_OPEN_MAX);
				long fd;
				for (fd = 0; fd <= fd_max; fd++) {
					::close(fd);
				}
				return;
			}

			std::int32_t dfd = ::dirfd(d);
			struct dirent* ent;
			while ((ent = ::readdir(d)) != nullptr) {
				if (ent->d_name[0] >= '0' && ent->d_name[0] <= '9') {
					const char* p = &ent->d_name[1];
					std::int32_t fd = ent->d_name[0] - '0';
					while (*p >= '0' && *p <= '9') {
						fd = (10 * fd) + *(p++) - '0';
					}
					if (*p || fd == dfd) {
						continue;
					}
					::close(fd);
				}
			}
			::closedir(d);
		}
#	endif
#endif

#if defined(DEATH_TARGET_EMSCRIPTEN)
		EM_JS(int, __asyncjs__MountAsPersistent, (const char* path, int pathLength), {
			return Asyncify.handleSleep(function(callback) {
				var p = UTF8ToString(path, pathLength);

				FS.mkdir(p);
				FS.mount(IDBFS, { }, p);

				FS.syncfs(true, function(error) {
					callback(error ? 0 : 1);
				});
			});
		});
#endif
	}

	String FileSystem::_savePath;

	class FileSystem::Directory::Impl
	{
		friend class Directory;

	public:
		Impl(const StringView path, EnumerationOptions options)
			: _fileNamePart(nullptr)
#if defined(DEATH_TARGET_WINDOWS)
				, _hFindFile(NULL)
#else
#	if defined(DEATH_TARGET_ANDROID)
				, _assetDir(nullptr)
#	endif
				, _dirStream(nullptr)
#endif
		{
			Open(path, options);
		}

		~Impl()
		{
			Close();
		}

		Impl(const Impl&) = delete;
		Impl& operator=(const Impl&) = delete;

		bool Open(const StringView path, EnumerationOptions options)
		{
			Close();

			_options = options;
			_path[0] = '\0';
#if defined(DEATH_TARGET_WINDOWS)
			if (!path.empty() && DirectoryExists(path)) {
				// Prepare full path to found files
				{
					String absPath = GetAbsolutePath(path);
					std::size_t pathLength = absPath.size();
					std::memcpy(_path, absPath.data(), pathLength);
					if (_path[pathLength - 1] == '/' || _path[pathLength - 1] == '\\') {
						_path[pathLength - 1] = '\\';
						_path[pathLength] = '\0';
						_fileNamePart = _path + pathLength;
					} else {
						_path[pathLength] = '\\';
						_path[pathLength + 1] = '\0';
						_fileNamePart = _path + pathLength + 1;
					}
				}

				Array<wchar_t> buffer = Utf8::ToUtf16(_path, static_cast<std::int32_t>(_fileNamePart - _path));
				if (buffer.size() + 2 <= MaxPathLength) {
					auto bufferExtended = Array<wchar_t>(NoInit, buffer.size() + 2);
					std::memcpy(bufferExtended.data(), buffer.data(), buffer.size() * sizeof(wchar_t));

					// Adding a wildcard to list all files in the directory
					bufferExtended[buffer.size()] = L'*';
					bufferExtended[buffer.size() + 1] = L'\0';

					WIN32_FIND_DATA data;
#	if defined(DEATH_TARGET_WINDOWS_RT)
					_hFindFile = ::FindFirstFileExFromAppW(bufferExtended, FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
#	else
					_hFindFile = ::FindFirstFileExW(bufferExtended, Environment::IsWindows7() ? FindExInfoBasic : FindExInfoStandard,
						&data, FindExSearchNameMatch, nullptr, Environment::IsWindows7() ? FIND_FIRST_EX_LARGE_FETCH : 0);
#	endif
					if (_hFindFile != NULL && _hFindFile != INVALID_HANDLE_VALUE) {
						if ((data.cFileName[0] == L'.' && (data.cFileName[1] == L'\0' || (data.cFileName[1] == L'.' && data.cFileName[2] == L'\0'))) ||
							((_options & EnumerationOptions::SkipDirectories) == EnumerationOptions::SkipDirectories && (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY) ||
							((_options & EnumerationOptions::SkipFiles) == EnumerationOptions::SkipFiles && (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY)) {
							// Skip this file
							Increment();
						} else {
							strncpy_s(_fileNamePart, sizeof(_path) - (_fileNamePart - _path), Utf8::FromUtf16(data.cFileName).data(), MaxPathLength - 1);
						}
					}
				}
			}
			return (_hFindFile != NULL && _hFindFile != INVALID_HANDLE_VALUE);
#else
			auto nullTerminatedPath = String::nullTerminatedView(path);
			if (!nullTerminatedPath.empty()) {
#	if defined(DEATH_TARGET_ANDROID)
				const char* assetPath = AndroidAssetStream::TryGetAssetPath(nullTerminatedPath.data());
				if (assetPath != nullptr) {
					// It probably supports only files
					if ((_options & EnumerationOptions::SkipFiles) != EnumerationOptions::SkipFiles) {
						_assetDir = AndroidAssetStream::OpenDirectory(assetPath);
						if (_assetDir != nullptr) {
							std::size_t pathLength = path.size();
							std::memcpy(_path, path.data(), pathLength);
							if (_path[pathLength - 1] == '/' || _path[pathLength - 1] == '\\') {
								_path[pathLength - 1] = '/';
								_path[pathLength] = '\0';
								_fileNamePart = _path + pathLength;
							} else {
								_path[pathLength] = '/';
								_path[pathLength + 1] = '\0';
								_fileNamePart = _path + pathLength + 1;
							}
							return true;
						}
					}
					return false;
				}
#	endif
				_dirStream = ::opendir(nullTerminatedPath.data());
				if (_dirStream != nullptr) {
					String absPath = GetAbsolutePath(path);
					std::size_t pathLength = absPath.size();
					std::memcpy(_path, absPath.data(), pathLength);
					if (_path[pathLength - 1] == '/' || _path[pathLength - 1] == '\\') {
						_path[pathLength - 1] = '/';
						_path[pathLength] = '\0';
						_fileNamePart = _path + pathLength;
					} else {
						_path[pathLength] = '/';
						_path[pathLength + 1] = '\0';
						_fileNamePart = _path + pathLength + 1;
					}
					return true;
				}
			}
			return false;
#endif
		}

		void Close()
		{
#if defined(DEATH_TARGET_WINDOWS)
			if (_hFindFile != NULL && _hFindFile != INVALID_HANDLE_VALUE) {
				::FindClose(_hFindFile);
				_hFindFile = NULL;
			}
#else
#	if defined(DEATH_TARGET_ANDROID)
			if (_assetDir != nullptr) {
				AndroidAssetStream::CloseDirectory(_assetDir);
				_assetDir = nullptr;
			} else
#	endif
			if (_dirStream != nullptr) {
				::closedir(_dirStream);
				_dirStream = nullptr;
			}
#endif
		}

		void Increment()
		{
#if defined(DEATH_TARGET_WINDOWS)
			if (_hFindFile == NULL || _hFindFile == INVALID_HANDLE_VALUE) {
				_path[0] = '\0';
				return;
			}

		Retry:
			WIN32_FIND_DATA data;
			if (::FindNextFileW(_hFindFile, &data)) {
				if ((data.cFileName[0] == L'.' && (data.cFileName[1] == L'\0' || (data.cFileName[1] == L'.' && data.cFileName[2] == L'\0'))) ||
					((_options & EnumerationOptions::SkipDirectories) == EnumerationOptions::SkipDirectories && (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY) ||
					((_options & EnumerationOptions::SkipFiles) == EnumerationOptions::SkipFiles && (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY)) {
					goto Retry;
				} else {
					strncpy_s(_fileNamePart, sizeof(_path) - (_fileNamePart - _path), Utf8::FromUtf16(data.cFileName).data(), MaxPathLength - 1);
				}
			} else {
				_path[0] = '\0';
			}
#else
#	if defined(DEATH_TARGET_ANDROID)
			// It does not return directory names
			if (_assetDir != nullptr) {
				const char* assetName = AndroidAssetStream::GetNextFileName(_assetDir);
				if (assetName == nullptr) {
					_path[0] = '\0';
					return;
				}
				strcpy(_fileNamePart, assetName);
				return;
			}
#	endif
			if (_dirStream == nullptr) {
				_path[0] = '\0';
				return;
			}

			struct dirent* entry;
		Retry:
			entry = ::readdir(_dirStream);
			if (entry != nullptr) {
				if (entry->d_name[0] == L'.' && (entry->d_name[1] == L'\0' || (entry->d_name[1] == L'.' && entry->d_name[2] == L'\0'))) {
					goto Retry;
				}

				if ((_options & EnumerationOptions::SkipDirectories) == EnumerationOptions::SkipDirectories && entry->d_type == DT_DIR)
					goto Retry;
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
				if ((_options & EnumerationOptions::SkipFiles) == EnumerationOptions::SkipFiles && entry->d_type == DT_REG)
					goto Retry;
				if ((_options & EnumerationOptions::SkipSpecial) == EnumerationOptions::SkipSpecial && entry->d_type != DT_DIR && entry->d_type != DT_REG && entry->d_type != DT_LNK)
					goto Retry;
#	else
				// Emscripten doesn't set DT_REG for files, so we treat everything that's not a DT_DIR as a file. SkipSpecial has no effect here.
				if ((_options & EnumerationOptions::SkipFiles) == EnumerationOptions::SkipFiles && entry->d_type != DT_DIR)
					goto Retry;
#	endif
				std::size_t charsLeft = sizeof(_path) - (_fileNamePart - _path) - 1;
				std::size_t fileLength = strlen(entry->d_name);
				if (fileLength > charsLeft) {
					// Path is too long, skip this file
					goto Retry;
				}
				strcpy(_fileNamePart, entry->d_name);
			} else {
				_path[0] = '\0';
			}
#endif
		}

	private:

		EnumerationOptions _options;
		char _path[MaxPathLength];
		char* _fileNamePart;
#if defined(DEATH_TARGET_WINDOWS)
		void* _hFindFile;
#else
#	if defined(DEATH_TARGET_ANDROID)
		AAssetDir* _assetDir;
#	endif
		DIR* _dirStream;
#endif
	};

	FileSystem::Directory::Directory() noexcept
	{
	}

	FileSystem::Directory::Directory(const StringView path, EnumerationOptions options)
		: _impl(std::make_shared<Impl>(path, options))
	{
	}

	FileSystem::Directory::~Directory()
	{
	}

	FileSystem::Directory::Directory(const Directory& other)
		: _impl(other._impl)
	{
	}

	FileSystem::Directory::Directory(Directory&& other) noexcept
		: _impl(std::move(other._impl))
	{
	}

	FileSystem::Directory& FileSystem::Directory::operator=(const Directory& other)
	{
		_impl = other._impl;
		return *this;
	}

	FileSystem::Directory& FileSystem::Directory::operator=(Directory&& other) noexcept
	{
		_impl = std::move(other._impl);
		return *this;
	}

	StringView FileSystem::Directory::operator*() const & noexcept
	{
		return _impl->_path;
	}

	FileSystem::Directory& FileSystem::Directory::operator++()
	{
		_impl->Increment();
		return *this;
	}

	bool FileSystem::Directory::operator==(const Directory& other) const
	{
		bool isEnd1 = (_impl == nullptr || _impl->_path[0] == '\0');
		bool isEnd2 = (other._impl == nullptr || other._impl->_path[0] == '\0');
		if (isEnd1 || isEnd2) {
			return (isEnd1 && isEnd2);
		}
		return (_impl == other._impl);
	}

	bool FileSystem::Directory::operator!=(const Directory& other) const
	{
		return !(*this == other);
	}

	FileSystem::Directory::Proxy::Proxy(const Containers::StringView path)
		: _path(path)
	{
	}

	StringView FileSystem::Directory::Proxy::operator*() const & noexcept
	{
		return _path;
	}

#if !defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_SWITCH)
	String FileSystem::FindPathCaseInsensitive(const StringView path)
	{
		if (Exists(path)) {
			return path;
		}

		std::size_t l = path.size();
		char* p = (char*)alloca(l + 1);
		strncpy(p, path.data(), l);
		p[l] = '\0';
		std::size_t rl = 0;
		bool isAbsolute = (p[0] == '/' || p[0] == '\\');

		String result(NoInit, path.size() + (isAbsolute ? 0 : 2));

		DIR* d;
		if (isAbsolute) {
			d = ::opendir("/");
			p = p + 1;
		} else {
			d = ::opendir(".");
			result[0] = '.';
			result[1] = '\0';
			rl = 1;
		}

		bool last = false;
		char* c = strsep(&p, "/");
		while (c) {
			if (d == nullptr) {
				return { };
			}

			if (last) {
				::closedir(d);
				return { };
			}

			result[rl] = '/';
			rl += 1;
			result[rl] = '\0';

			struct dirent* entry = ::readdir(d);
			while (entry != nullptr) {
				if (::strcasecmp(c, entry->d_name) == 0) {
					strcpy(&result[rl], entry->d_name);
					rl += strlen(entry->d_name);

					::closedir(d);
					d = ::opendir(result.data());
					break;
				}

				entry = ::readdir(d);
			}

			if (entry == nullptr) {
				strcpy(&result[rl], c);
				rl += strlen(c);
				last = true;
			}

			c = strsep(&p, "/");
		}

		if (d != nullptr) {
			::closedir(d);
		}
		return result;
	}
#endif

	String FileSystem::CombinePath(const StringView first, const StringView second)
	{
		std::size_t firstSize = first.size();
		std::size_t secondSize = second.size();

		if (secondSize == 0) {
			return first;
		}
		if (firstSize == 0 || GetPathRootLength(second) > 0) {
			return second;
		}

#	if defined(DEATH_TARGET_ANDROID)
		if (first == AndroidAssetStream::Prefix) {
			return first + second;
		}
#	endif

		if (first[firstSize - 1] == '/' || first[firstSize - 1] == '\\') {
			// Path has trailing separator
			return first + second;
		} else {
			// Both paths have no clashing separators
#if defined(DEATH_TARGET_WINDOWS)
			return "\\"_s.join({ first, second });
#else
			return "/"_s.join({ first, second });
#endif
		}
	}

	String FileSystem::CombinePath(const ArrayView<const StringView> paths)
	{
		if (paths.empty()) return { };

		std::size_t count = paths.size();
		std::size_t resultSize = 0;
		std::size_t startIdx = 0;
		for (std::size_t i = 0; i < count; i++) {
			std::size_t pathSize = paths[i].size();
			if (pathSize == 0) {
				continue;
			}
			if (GetPathRootLength(paths[i]) > 0) {
				resultSize = 0;
				startIdx = i;
			}

			resultSize += pathSize;

			if (i + 1 < count && paths[i][pathSize - 1] != '/' && paths[i][pathSize - 1] != '\\') {
				resultSize++;
			}
		}

		String result{NoInit, resultSize};
		resultSize = 0;
		for (std::size_t i = startIdx; i < count; i++) {
			std::size_t pathSize = paths[i].size();
			if (pathSize == 0) {
				continue;
			}

			std::memcpy(&result[resultSize], paths[i].data(), pathSize);
			resultSize += pathSize;

			if (i + 1 < count && paths[i][pathSize - 1] != '/' && paths[i][pathSize - 1] != '\\') {
#if defined(DEATH_TARGET_WINDOWS)
				result[resultSize] = '\\';
#else
				result[resultSize] = '/';
#endif
				resultSize++;
			}
		}

		return result;
	}

	String FileSystem::CombinePath(const std::initializer_list<StringView> paths)
	{
		return CombinePath(Containers::arrayView(paths));
	}

	StringView FileSystem::GetDirectoryName(const StringView path)
	{
		if (path.empty()) return { };

		std::size_t pathRootLength = GetPathRootLength(path);
		std::size_t i = path.size();
		// Strip any trailing path separators
		while (i > pathRootLength && (path[i - 1] == '/' || path[i - 1] == '\\')) {
			i--;
		}
		if (i <= pathRootLength) return { };
		// Try to get the last path separator
		while (i > pathRootLength && path[--i] != '/' && path[i] != '\\');

		return path.slice(0, i);
	}

	StringView FileSystem::GetFileName(const StringView path)
	{
		if (path.empty()) return { };

		std::size_t pathRootLength = GetPathRootLength(path);
		std::size_t pathLength = path.size();
		// Strip any trailing path separators
		while (pathLength > pathRootLength && (path[pathLength - 1] == '/' || path[pathLength - 1] == '\\')) {
			pathLength--;
		}
		if (pathLength <= pathRootLength) return { };
		std::size_t i = pathLength;
		// Try to get the last path separator
		while (i > pathRootLength && path[--i] != '/' && path[i] != '\\');

		if (path[i] == '/' || path[i] == '\\') {
			i++;
		}
		return path.slice(i, pathLength);
	}

	StringView FileSystem::GetFileNameWithoutExtension(const StringView path)
	{
		StringView fileName = GetFileName(path);
		if (fileName.empty()) return { };

		const StringView foundDot = fileName.findLastOr('.', fileName.end());
		if (foundDot.begin() == fileName.end()) return fileName;

		bool initialDots = true;
		for (char c : fileName.prefix(foundDot.begin())) {
			if (c != '.') {
				initialDots = false;
				break;
			}
		}
		if (initialDots) return fileName;
		return fileName.prefix(foundDot.begin());
	}

	String FileSystem::GetExtension(const StringView path)
	{
		StringView fileName = GetFileName(path);
		if (fileName.empty()) return { };

		const StringView foundDot = fileName.findLastOr('.', fileName.end());
		if (foundDot.begin() == fileName.end()) return { };

		bool initialDots = true;
		for (char c : fileName.prefix(foundDot.begin())) {
			if (c != '.') {
				initialDots = false;
				break;
			}
		}
		if (initialDots) return { };
		String result = fileName.suffix(foundDot.begin() + 1);

		// Convert to lower-case
		for (char& c : result) {
			if (c >= 'A' && c <= 'Z') {
				c |= 0x20;
			}
		}

		return result;
	}

#if defined(DEATH_TARGET_WINDOWS)
	String FileSystem::ToNativeSeparators(String path)
	{
		// Take ownership first if not already (e.g., directly from `String::nullTerminatedView()`)
		if (!path.isSmall() && path.deleter()) {
			path = String{path};
		}
		for (char& c : path) {
			if (c == '/') {
				c = '\\';
			}
		}
		return path;
	}
#endif

	String FileSystem::GetAbsolutePath(const StringView path)
	{
		if (path.empty()) return { };

#if defined(DEATH_TARGET_WINDOWS)
		wchar_t buffer[MaxPathLength];
		DWORD length = ::GetFullPathNameW(Utf8::ToUtf16(path), static_cast<DWORD>(arraySize(buffer)), buffer, nullptr);
		if (length == 0) {
			return { };
		}
		return Utf8::FromUtf16(buffer, length);
#elif defined(DEATH_TARGET_SWITCH)
		// realpath() is missing in libnx
		char left[MaxPathLength];
		char nextToken[MaxPathLength];
		char result[MaxPathLength];
		std::size_t resultLength = 0;
#	if !defined(DEATH_TARGET_SWITCH)
		std::int32_t symlinks = 0;
#	endif

		std::size_t pathRootLength = GetPathRootLength(path);
		if (pathRootLength > 0) {
			strncpy(result, path.data(), pathRootLength);
			resultLength = pathRootLength;
			if (path.size() == pathRootLength) {
				return String{result, resultLength};
			}

			strncpy(left, path.data() + pathRootLength, sizeof(left));
		} else {
			if (::getcwd(result, sizeof(result)) == nullptr) {
				return "."_s;
			}
			resultLength = strlen(result);
			strncpy(left, path.data(), sizeof(left));
		}
		std::size_t leftLength = strlen(left);
		if (leftLength >= sizeof(left) || resultLength >= MaxPathLength) {
			// Path is too long
			return path;
		}

		while (leftLength != 0) {
			char* p = strchr(left, '/');
			char* s = (p != nullptr ? p : left + leftLength);
			std::size_t nextTokenLength = s - left;
			if (nextTokenLength >= sizeof(nextToken)) {
				// Path is too long
				return path;
			}
			std::memcpy(nextToken, left, nextTokenLength);
			nextToken[nextTokenLength] = '\0';
			leftLength -= nextTokenLength;
			if (p != nullptr) {
				std::memmove(left, s + 1, leftLength + 1);
				leftLength--;
			}
			if (result[resultLength - 1] != '/') {
				if (resultLength + 1 >= MaxPathLength) {
					return path;
				}
				result[resultLength++] = '/';
			}
			if (nextToken[0] == '\0' || strcmp(nextToken, ".") == 0) {
				continue;
			}
			if (strcmp(nextToken, "..") == 0) {
				if (resultLength > 1) {
					result[resultLength - 1] = '\0';
					char* q = strrchr(result, '/') + 1;
					resultLength = q - result;
				}
				continue;
			}

			if (resultLength + nextTokenLength >= sizeof(result)) {
				// Path is too long
				return path;
			}
			std::memcpy(result + resultLength, nextToken, nextTokenLength);
			resultLength += nextTokenLength;
			result[resultLength] = '\0';

			struct stat sb;
			if (::lstat(result, &sb) != 0) {
				if (errno == ENOENT && p == nullptr) {
					return String{result, resultLength};
				}
				return { };
			}
#	if !defined(DEATH_TARGET_SWITCH)
			// readlink() is missing in libnx
			if (S_ISLNK(sb.st_mode)) {
				if (++symlinks > 8) {
					// Too many symlinks
					return { };
				}
				ssize_t symlinkLength = ::readlink(result, nextToken, sizeof(nextToken) - 1);
				if (symlinkLength < 0) {
					// Cannot resolve the symlink
					return { };
				}
				nextToken[symlinkLength] = '\0';
				if (nextToken[0] == '/') {
					resultLength = 1;
				} else if (resultLength > 1) {
					result[resultLength - 1] = '\0';
					char* q = strrchr(result, '/') + 1;
					resultLength = q - result;
				}

				if (p != nullptr) {
					if (nextToken[symlinkLength - 1] != '/') {
						if (static_cast<std::size_t>(symlinkLength) + 1 >= sizeof(nextToken)) {
							// Path is too long
							return { };
						}
						nextToken[symlinkLength++] = '/';
					}
					strncpy(nextToken + symlinkLength, left, sizeof(nextToken) - symlinkLength);
				}
				strncpy(left, nextToken, sizeof(left));
				leftLength = strlen(left);
			}
#	endif
		}

		if (resultLength > 1 && result[resultLength - 1] == '/') {
			resultLength--;
		}
		return String{result, resultLength};
#else
#	if defined(DEATH_TARGET_ANDROID)
		if (path.hasPrefix(AndroidAssetStream::Prefix)) {
			return path;
		}
#	endif
		char buffer[MaxPathLength];
		const char* resolvedPath = ::realpath(String::nullTerminatedView(path).data(), buffer);
		if (resolvedPath == nullptr) {
			return { };
		}
		return buffer;
#endif
	}

	bool FileSystem::IsAbsolutePath(const StringView path)
	{
		return GetPathRootLength(path) > 0;
	}

	String FileSystem::GetExecutablePath()
	{
#if defined(DEATH_TARGET_EMSCRIPTEN)
		return "/"_s;
#elif defined(DEATH_TARGET_APPLE)
		// Get path size (need to set it to 0 to avoid filling nullptr with random data and crashing)
		std::uint32_t size = 0;
		if (_NSGetExecutablePath(nullptr, &size) != -1) {
			return { };
		}

		// Allocate proper size and get the path. The size includes a null terminator which the String handles on its own, so subtract it
		String path{NoInit, size - 1};
		if (_NSGetExecutablePath(path.data(), &size) != 0) {
			return { };
		}
		return path;
#elif defined(__FreeBSD__)
		std::size_t size;
		static const std::int32_t mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
		sysctl(mib, 4, nullptr, &size, nullptr, 0);
		String path{NoInit, size};
		sysctl(mib, 4, path.data(), &size, nullptr, 0);
		return path;
#elif defined(DEATH_TARGET_UNIX)
		// Reallocate like hell until we have enough place to store the path. Can't use lstat because
		// the /proc/self/exe symlink is not a real symlink and so stat::st_size returns 0.
		static const char self[] = "/proc/self/exe";
		Array<char> path;
		arrayResize(path, NoInit, 4);
		ssize_t size;
		while ((size = ::readlink(self, path, path.size())) == ssize_t(path.size())) {
			arrayResize(path, NoInit, path.size() * 2);
		}

		// readlink() doesn't put the null terminator into the array, do it ourselves. The above loop guarantees
		// that path.size() is always larger than size - if it would be equal, we'd try once more with a larger buffer
		path[size] = '\0';
		const auto deleter = path.deleter();
		return String{path.release(), std::size_t(size), deleter};
#elif defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		wchar_t path[MaxPathLength + 1];
		// Returns size *without* the null terminator
		const std::size_t size = ::GetModuleFileNameW(NULL, path, static_cast<DWORD>(arraySize(path)));
		return Utf8::FromUtf16(arrayView(path, size));
#else
		return { };
#endif
	}

	String FileSystem::GetWorkingDirectory()
	{
#if defined(DEATH_TARGET_EMSCRIPTEN)
		return "/"_s;
#elif defined(DEATH_TARGET_WINDOWS)
		wchar_t buffer[MaxPathLength];
		::GetCurrentDirectoryW(MaxPathLength, buffer);
		return Utf8::FromUtf16(buffer);
#else
		char buffer[MaxPathLength];
		if (::getcwd(buffer, MaxPathLength) == nullptr) {
			return { };
		}
		return buffer;
#endif
	}

	bool FileSystem::SetWorkingDirectory(const StringView path)
	{
#if defined(DEATH_TARGET_EMSCRIPTEN)
		return false;
#elif defined(DEATH_TARGET_WINDOWS)
		return ::SetCurrentDirectoryW(Utf8::ToUtf16(path));
#else
		return (::chdir(String::nullTerminatedView(path).data()) == 0);
#endif
	}

	String FileSystem::GetHomeDirectory()
	{
#if defined(DEATH_TARGET_WINDOWS_RT)
		// This method is not supported on WinRT
		return { };
#elif defined(DEATH_TARGET_WINDOWS)
		wchar_t buffer[MaxPathLength];
		::SHGetFolderPathW(HWND_DESKTOP, CSIDL_PROFILE, NULL, SHGFP_TYPE_CURRENT, buffer);
		return Utf8::FromUtf16(buffer);
#else
		const char* home = ::getenv("HOME");
		if (home != nullptr && home[0] != '\0') {
			return home;
		}
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
		// `getpwuid()` is not yet implemented on Emscripten
		const struct passwd* pw = ::getpwuid(getuid());
		if (pw != nullptr) {
			return pw->pw_dir;
		}
#	endif
		return { };
#endif
	}

#if defined(DEATH_TARGET_ANDROID)
	String FileSystem::GetExternalStorage()
	{
		StringView extStorage = ::getenv("EXTERNAL_STORAGE");
		if (!extStorage.empty()) {
			return extStorage;
		}
		return "/sdcard"_s;
	}
#elif defined(DEATH_TARGET_UNIX)
	String FileSystem::GetLocalStorage()
	{
		StringView localStorage = ::getenv("XDG_DATA_HOME");
		if (IsAbsolutePath(localStorage)) {
			return localStorage;
		}

		// Not delegating into GetHomeDirectory() as the (admittedly rare) error message would have a confusing source
		StringView home = ::getenv("HOME");
		if (!home.empty()) {
			return CombinePath(home, ".local/share/"_s);
		}

		return { };
	}
#elif defined(DEATH_TARGET_WINDOWS)
	String FileSystem::GetWindowsDirectory()
	{
		wchar_t buffer[MaxPathLength];
		UINT requiredLength = ::GetSystemWindowsDirectoryW(buffer, static_cast<UINT>(MaxPathLength));
		if (requiredLength == 0 || requiredLength >= MaxPathLength) return { };
		return Utf8::FromUtf16(buffer);
	}
#endif

	bool FileSystem::DirectoryExists(const StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return (::GetFileAttributesExFromAppW(Utf8::ToUtf16(path), GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY);
#elif defined(DEATH_TARGET_WINDOWS)
		const DWORD attrs = ::GetFileAttributesW(Utf8::ToUtf16(path));
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath.data())) {
			return AndroidAssetStream::TryOpenDirectory(nullTerminatedPath.data());
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) == 0) {
			return ((sb.st_mode & S_IFMT) == S_IFDIR);
		}
		return false;
#endif
	}

	bool FileSystem::FileExists(const StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return (::GetFileAttributesExFromAppW(Utf8::ToUtf16(path), GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY);
#elif defined(DEATH_TARGET_WINDOWS)
		const DWORD attrs = ::GetFileAttributesW(Utf8::ToUtf16(path));
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath.data())) {
			return AndroidAssetStream::TryOpenFile(nullTerminatedPath.data());
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) == 0) {
			return ((sb.st_mode & S_IFMT) == S_IFREG);
		}
		return false;
#endif
	}

	bool FileSystem::Exists(const StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return !(!::GetFileAttributesExFromAppW(Utf8::ToUtf16(path), GetFileExInfoStandard, &lpFileInfo) && ::GetLastError() == ERROR_FILE_NOT_FOUND);
#elif defined(DEATH_TARGET_WINDOWS)
		const DWORD attrs = ::GetFileAttributesW(Utf8::ToUtf16(path));
		return !(attrs == INVALID_FILE_ATTRIBUTES && ::GetLastError() == ERROR_FILE_NOT_FOUND);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath.data())) {
			return AndroidAssetStream::TryOpen(nullTerminatedPath.data());
		}
#	endif
		struct stat sb;
		return (::lstat(nullTerminatedPath.data(), &sb) == 0);
#endif
	}

	bool FileSystem::IsReadable(const StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return ::GetFileAttributesExFromAppW(Utf8::ToUtf16(path), GetFileExInfoStandard, &lpFileInfo);
#elif defined(DEATH_TARGET_WINDOWS)
		const DWORD attrs = ::GetFileAttributesW(Utf8::ToUtf16(path));
		return (attrs != INVALID_FILE_ATTRIBUTES);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath.data())) {
			return AndroidAssetStream::TryOpen(nullTerminatedPath.data());
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) == 0) {
			return ((sb.st_mode & S_IRUSR) != 0);
		}
		return false;
#endif
	}

	bool FileSystem::IsWritable(const StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return (::GetFileAttributesExFromAppW(Utf8::ToUtf16(path), GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_READONLY) == 0);
#elif defined(DEATH_TARGET_WINDOWS)
		const DWORD attrs = ::GetFileAttributesW(Utf8::ToUtf16(path));
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY) == 0);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath.data())) {
			return false;
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) == 0) {
			return ((sb.st_mode & S_IWUSR) != 0);
		}
		return false;
#endif
	}

	bool FileSystem::IsExecutable(const StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		return false;
#elif defined(DEATH_TARGET_WINDOWS)
		// Assuming that every file that exists is also executable
		const DWORD attrs = ::GetFileAttributesW(Utf8::ToUtf16(path));
		// Assuming that every existing directory is accessible
		if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY) {
			return true;
		} else if (attrs != INVALID_FILE_ATTRIBUTES) {
			// Using some of the Windows executable extensions to detect executable files
			auto extension = GetExtension(path);
			return (extension == "exe"_s || extension == "bat"_s || extension == "com"_s);
		} else {
			return false;
		}
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath.data())) {
			return AndroidAssetStream::TryOpenDirectory(nullTerminatedPath.data());
		}
#	endif
		return (::access(nullTerminatedPath.data(), X_OK) == 0);
#endif
	}

	bool FileSystem::IsReadableFile(const StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return (::GetFileAttributesExFromAppW(Utf8::ToUtf16(path), GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY);
#elif defined(DEATH_TARGET_WINDOWS)
		const DWORD attrs = ::GetFileAttributesW(Utf8::ToUtf16(path));
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath.data())) {
			return AndroidAssetStream::TryOpenFile(nullTerminatedPath.data());
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) == 0) {
			return ((sb.st_mode & S_IFMT) == S_IFREG && (sb.st_mode & S_IRUSR) != 0);
		}
#endif
		return false;
	}

	bool FileSystem::IsWritableFile(const StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return (::GetFileAttributesExFromAppW(Utf8::ToUtf16(path), GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY)) == 0);
#elif defined(DEATH_TARGET_WINDOWS)
		const DWORD attrs = ::GetFileAttributesW(Utf8::ToUtf16(path));
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY)) == 0);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath.data())) {
			return false;
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) == 0) {
			return ((sb.st_mode & S_IFMT) == S_IFREG && (sb.st_mode & S_IWUSR) != 0);
		}
#endif
		return false;
	}

	bool FileSystem::IsSymbolicLink(const StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return (::GetFileAttributesExFromAppW(Utf8::ToUtf16(path), GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == FILE_ATTRIBUTE_REPARSE_POINT);
#elif defined(DEATH_TARGET_WINDOWS)
		const DWORD attrs = ::GetFileAttributesW(Utf8::ToUtf16(path));
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT) == FILE_ATTRIBUTE_REPARSE_POINT);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath.data())) {
			return false;
		}
#	endif
		struct stat sb;
		if (::lstat(nullTerminatedPath.data(), &sb) == 0) {
			return ((sb.st_mode & S_IFMT) == S_IFLNK);
		}
#endif
		return false;
	}

	bool FileSystem::IsHidden(const StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return (::GetFileAttributesExFromAppW(Utf8::ToUtf16(path), GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) == FILE_ATTRIBUTE_HIDDEN);
#elif defined(DEATH_TARGET_WINDOWS)
		const DWORD attrs = ::GetFileAttributesW(Utf8::ToUtf16(path));
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_HIDDEN) == FILE_ATTRIBUTE_HIDDEN);
#elif defined(DEATH_TARGET_APPLE) || defined(__FreeBSD__)
		auto nullTerminatedPath = String::nullTerminatedView(path);
		struct stat sb;
		return (::stat(nullTerminatedPath.data(), &sb) == 0 && (sb.st_flags & UF_HIDDEN) == UF_HIDDEN);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath.data())) {
			return false;
		}
#	endif
		char buffer[MaxPathLength];
		std::size_t pathLength = std::min((std::size_t)MaxPathLength - 1, path.size());
		strncpy(buffer, path.data(), pathLength);
		buffer[pathLength] = '\0';
		const char* baseName = ::basename(buffer);
		return (baseName != nullptr && baseName[0] == '.');
#endif
	}

	bool FileSystem::SetHidden(const StringView path, bool hidden)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		Array<wchar_t> nullTerminatedPath = Utf8::ToUtf16(path);
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		if (!::GetFileAttributesExFromAppW(nullTerminatedPath, GetFileExInfoStandard, &lpFileInfo)) {
			return false;
		}

		if (hidden == ((lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) == FILE_ATTRIBUTE_HIDDEN)) {
			return true;
		} else if (hidden) {
			return ::SetFileAttributes(nullTerminatedPath, lpFileInfo.dwFileAttributes | FILE_ATTRIBUTE_HIDDEN);
		} else {
			return ::SetFileAttributes(nullTerminatedPath, lpFileInfo.dwFileAttributes & ~FILE_ATTRIBUTE_HIDDEN);
		}
#elif defined(DEATH_TARGET_WINDOWS)
		Array<wchar_t> nullTerminatedPath = Utf8::ToUtf16(path);
		DWORD attrs = ::GetFileAttributesW(nullTerminatedPath);
		if (attrs == INVALID_FILE_ATTRIBUTES) {
			return false;
		}

		if (hidden == ((attrs & FILE_ATTRIBUTE_HIDDEN) == FILE_ATTRIBUTE_HIDDEN)) {
			return true;
		} else if (hidden) {
			return ::SetFileAttributesW(nullTerminatedPath, attrs | FILE_ATTRIBUTE_HIDDEN);
		} else {
			return ::SetFileAttributesW(nullTerminatedPath, attrs & ~FILE_ATTRIBUTE_HIDDEN);
		}
#elif defined(DEATH_TARGET_APPLE) || defined(__FreeBSD__)
		auto nullTerminatedPath = String::nullTerminatedView(path);
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) != 0) {
			return false;
		}

		if (hidden == ((sb.st_flags & UF_HIDDEN) == UF_HIDDEN)) {
			return true;
		} else if (hidden) {
			return ::chflags(nullTerminatedPath.data(), sb.st_flags | UF_HIDDEN) == 0;
		} else {
			return ::chflags(nullTerminatedPath.data(), sb.st_flags & ~UF_HIDDEN) == 0;
		}
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath.data())) {
			return false;
		}
#	endif
		char buffer[MaxPathLength];
		std::size_t pathLength = std::min((std::size_t)MaxPathLength - 1, path.size());
		strncpy(buffer, nullTerminatedPath.data(), pathLength);
		buffer[pathLength] = '\0';
		const char* baseName = ::basename(buffer);
		if (hidden && baseName != nullptr && baseName[0] != '.') {
			String newPath = CombinePath(GetDirectoryName(nullTerminatedPath), String("."_s + baseName));
			return (::rename(nullTerminatedPath.data(), newPath.data()) == 0);
		} else if (!hidden && baseName != nullptr && baseName[0] == '.') {
			std::int32_t numDots = 0;
			while (baseName[numDots] == '.') {
				numDots++;
			}
			String newPath = CombinePath(GetDirectoryName(nullTerminatedPath), &buffer[numDots]);
			return (::rename(nullTerminatedPath.data(), newPath.data()) == 0);
		}
#endif
		return false;
	}

	bool FileSystem::IsReadOnly(const StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return (::GetFileAttributesExFromAppW(Utf8::ToUtf16(path), GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_READONLY) == FILE_ATTRIBUTE_READONLY);
#elif defined(DEATH_TARGET_WINDOWS)
		const DWORD attrs = ::GetFileAttributesW(Utf8::ToUtf16(path));
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY) == FILE_ATTRIBUTE_READONLY);
#elif defined(DEATH_TARGET_APPLE) || defined(__FreeBSD__)
		auto nullTerminatedPath = String::nullTerminatedView(path);
		struct stat sb;
		return (::stat(nullTerminatedPath.data(), &sb) == 0 && (sb.st_flags & UF_IMMUTABLE) == UF_IMMUTABLE);
#else
		return false;
#endif
	}

	bool FileSystem::SetReadOnly(const StringView path, bool readonly)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		Array<wchar_t> nullTerminatedPath = Utf8::ToUtf16(path);
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		if (!::GetFileAttributesExFromAppW(nullTerminatedPath, GetFileExInfoStandard, &lpFileInfo)) {
			return false;
		}

		if (readonly == ((lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_READONLY) == FILE_ATTRIBUTE_READONLY)) {
			return true;
		} else if (readonly) {
			return ::SetFileAttributes(nullTerminatedPath, lpFileInfo.dwFileAttributes | FILE_ATTRIBUTE_READONLY);
		} else {
			return ::SetFileAttributes(nullTerminatedPath, lpFileInfo.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
		}
#elif defined(DEATH_TARGET_WINDOWS)
		Array<wchar_t> nullTerminatedPath = Utf8::ToUtf16(path);
		DWORD attrs = ::GetFileAttributesW(nullTerminatedPath);
		if (attrs == INVALID_FILE_ATTRIBUTES) {
			return false;
		}

		if (readonly == ((attrs & FILE_ATTRIBUTE_READONLY) == FILE_ATTRIBUTE_READONLY)) {
			return true;
		} else if (readonly) {
			return ::SetFileAttributesW(nullTerminatedPath, attrs | FILE_ATTRIBUTE_READONLY);
		} else {
			return ::SetFileAttributesW(nullTerminatedPath, attrs & ~FILE_ATTRIBUTE_READONLY);
		}
#elif defined(DEATH_TARGET_APPLE) || defined(__FreeBSD__)
		auto nullTerminatedPath = String::nullTerminatedView(path);
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) != 0) {
			return false;
		}

		if (readonly == ((sb.st_flags & UF_IMMUTABLE) == UF_IMMUTABLE)) {
			return true;
		} else if (readonly) {
			return ::chflags(nullTerminatedPath.data(), sb.st_flags | UF_IMMUTABLE) == 0;
		} else {
			return ::chflags(nullTerminatedPath.data(), sb.st_flags & ~UF_IMMUTABLE) == 0;
		}
#endif
		return false;
	}

	bool FileSystem::CreateDirectories(const StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS)
		Array<wchar_t> fullPath = Utf8::ToUtf16(path);
		// Don't use DirectoryExists() to avoid calling Utf8::ToUtf16() twice
#	if defined(DEATH_TARGET_WINDOWS_RT)
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		if (::GetFileAttributesExFromAppW(fullPath, GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY) {
			return true;
		}
#	else
		const DWORD attrs = ::GetFileAttributesW(fullPath);
		if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY) {
			return true;
		}
#	endif

		std::int32_t fullPathSize = static_cast<std::int32_t>(fullPath.size());
		std::int32_t startIdx = 0;
		if (fullPathSize >= 2) {
			if (fullPath[0] == L'\\' && fullPath[1] == L'\\') {
				// Skip the first part of UNC paths ("\\.\", "\\?\" or "\\hostname\")
				startIdx = 3;
				while (fullPath[startIdx] != L'\\' && fullPath[startIdx] != L'\0') {
					startIdx++;
				}
				startIdx++;
			}
			if (fullPath[startIdx + 1] == L':') {
				startIdx += 3;
			}
		}
		if (startIdx == 0 && (fullPath[0] == L'/' || fullPath[0] == L'\\')) {
			startIdx = 1;
		}

		bool slashWasLast = true;
		for (std::int32_t i = startIdx; i < fullPathSize; i++) {
			if (fullPath[i] == L'\0') {
				break;
			}

			if (fullPath[i] == L'/' || fullPath[i] == L'\\') {
				fullPath[i] = L'\0';
#	if defined(DEATH_TARGET_WINDOWS_RT)
				if (!::GetFileAttributesExFromAppW(fullPath, GetFileExInfoStandard, &lpFileInfo)) {
					if (!::CreateDirectoryFromAppW(fullPath, NULL)) {
#	else
				const DWORD attrs = ::GetFileAttributesW(fullPath);
				if (attrs == INVALID_FILE_ATTRIBUTES) {
					if (!::CreateDirectoryW(fullPath, NULL)) {
#	endif
						DWORD error = ::GetLastError();
						if (error != ERROR_ALREADY_EXISTS) {
							LOGW("Cannot create directory \"%S\" with error 0x%08x%s", fullPath.data(), error, __GetWin32ErrorSuffix(error));
							return false;
						}
					}
				}
				fullPath[i] = L'\\';
				slashWasLast = true;
			} else {
				slashWasLast = false;
			}
		}

		if (!slashWasLast) {
#	if defined(DEATH_TARGET_WINDOWS_RT)
			if (!::CreateDirectoryFromAppW(fullPath, NULL)) {
#	else
			if (!::CreateDirectoryW(fullPath, NULL)) {
#	endif
				DWORD error = ::GetLastError();
				if (error != ERROR_ALREADY_EXISTS) {
					LOGW("Cannot create directory \"%S\" with error 0x%08x%s", fullPath.data(), error, __GetWin32ErrorSuffix(error));
					return false;
				}
			}
		}
		return true;
#else
		if (DirectoryExists(path)) {
			return true;
		}

		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath.data())) {
			return false;
		}
#	endif

		String fullPath = String{nullTerminatedPath};
		bool slashWasLast = true;
		struct stat sb;
		for (std::size_t i = 0; i < fullPath.size(); i++) {
			if (fullPath[i] == '\0') {
				break;
			}

			if (fullPath[i] == '/' || fullPath[i] == '\\') {
				if (i > 0) {
					fullPath[i] = '\0';
					if (::lstat(fullPath.data(), &sb) != 0) {
						if (::mkdir(fullPath.data(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0 && errno != EEXIST) {
							LOGW("Cannot create directory \"%s\"", fullPath.data());
							return false;
						}
					}
					fullPath[i] = '/';
				}
				slashWasLast = true;
			} else {
				slashWasLast = false;
			}
		}

		if (!slashWasLast) {
			if (::mkdir(fullPath.data(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0 && errno != EEXIST) {
				LOGW("Cannot create directory \"%s\"", fullPath.data());
				return false;
			}
		}
		return true;
#endif
	}

	bool FileSystem::RemoveDirectoryRecursive(const StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		if (!::GetFileAttributesExFromAppW(Utf8::ToUtf16(path), GetFileExInfoStandard, &lpFileInfo) || (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY) {
			return false;
		}

		// Do not recursively delete through reparse points
		Array<wchar_t> absPath = Utf8::ToUtf16(GetAbsolutePath(path));
		return DeleteDirectoryInternal(absPath, (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != FILE_ATTRIBUTE_REPARSE_POINT, 0);
#elif defined(DEATH_TARGET_WINDOWS)
		const DWORD attrs = ::GetFileAttributesW(Utf8::ToUtf16(path));
		if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY) {
			return false;
		}

		// Do not recursively delete through reparse points
		Array<wchar_t> absPath = Utf8::ToUtf16(GetAbsolutePath(path));
		return DeleteDirectoryInternal(absPath, (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != FILE_ATTRIBUTE_REPARSE_POINT, 0);
#else
		return DeleteDirectoryInternal(path);
#endif
	}

	bool FileSystem::RemoveFile(const StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		return ::DeleteFileFromAppW(Utf8::ToUtf16(path));
#elif defined(DEATH_TARGET_WINDOWS)
		return ::DeleteFileW(Utf8::ToUtf16(path));
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath.data())) {
			return false;
		}
#	endif
		return (::unlink(nullTerminatedPath.data()) == 0);
#endif
	}

	bool FileSystem::Move(const StringView oldPath, const StringView newPath)
	{
		if (oldPath.empty() || newPath.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS)
		return ::MoveFileExW(Utf8::ToUtf16(oldPath), Utf8::ToUtf16(newPath), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
#else
		auto nullTerminatedOldPath = String::nullTerminatedView(oldPath);
		auto nullTerminatedNewPath = String::nullTerminatedView(newPath);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedOldPath.data())) {
			return false;
		}
#	endif
		return (::rename(nullTerminatedOldPath.data(), nullTerminatedNewPath.data()) == 0);
#endif
	}

	bool FileSystem::MoveToTrash(const StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_APPLE)
		Class nsStringClass = objc_getClass("NSString");
		Class nsUrlClass = objc_getClass("NSURL");
		Class nsFileManager = objc_getClass("NSFileManager");
		if (nsStringClass != nullptr && nsUrlClass != nullptr && nsFileManager != nullptr) {
			id pathString = ((id(*)(Class, SEL, const char*))objc_msgSend)(nsStringClass, sel_getUid("stringWithUTF8String:"), String::nullTerminatedView(path).data());
			id pathUrl = ((id(*)(Class, SEL, id))objc_msgSend)(nsUrlClass, sel_getUid("fileURLWithPath:"), pathString);
			id fileManagerInstance = ((id(*)(Class, SEL))objc_msgSend)(nsFileManager, sel_getUid("defaultManager"));
			return ((bool(*)(id, SEL, id, SEL, id, SEL, id))objc_msgSend)(fileManagerInstance, sel_getUid("trashItemAtURL:"), pathUrl, sel_getUid("resultingItemURL:"), nullptr, sel_getUid("error:"), nullptr);
		}
		return false;
#elif defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		auto pathW = Utf8::ToUtf16(path);

		SHFILEOPSTRUCTW sf;
		sf.hwnd = NULL;
		sf.wFunc = FO_DELETE;
		sf.pFrom = pathW;
		sf.pTo = nullptr;
		sf.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
		sf.fAnyOperationsAborted = FALSE;
		sf.hNameMappings = nullptr;
		sf.lpszProgressTitle = nullptr;
		return ::SHFileOperationW(&sf) == 0;
#else
		return false;
#endif
	}

	bool FileSystem::Copy(const StringView oldPath, const StringView newPath, bool overwrite)
	{
		if (oldPath.empty() || newPath.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		return ::CopyFileFromAppW(Utf8::ToUtf16(oldPath), Utf8::ToUtf16(newPath), overwrite ? TRUE : FALSE);
#elif defined(DEATH_TARGET_WINDOWS)
		return ::CopyFileW(Utf8::ToUtf16(oldPath), Utf8::ToUtf16(newPath), overwrite ? TRUE : FALSE);
#else
		auto nullTerminatedOldPath = String::nullTerminatedView(oldPath);
		auto nullTerminatedNewPath = String::nullTerminatedView(newPath);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedOldPath.data())) {
			return false;
		}
#	endif
		if (!overwrite && Exists(newPath)) {
			return false;
		}

		std::int32_t sourceFd, destFd;
		if ((sourceFd = ::open(nullTerminatedOldPath.data(), O_RDONLY | O_CLOEXEC)) == -1) {
			return false;
		}

		struct stat sb;
		if (::fstat(sourceFd, &sb) != 0) {
			return false;
		}

		mode_t sourceMode = sb.st_mode;
		mode_t destMode = sourceMode;
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		// Enable writing for the newly created files, needed for some file systems
		destMode |= S_IWUSR;
#endif
		if ((destFd = ::open(nullTerminatedNewPath.data(), O_WRONLY | O_CLOEXEC | O_CREAT | O_TRUNC, destMode)) == -1) {
			::close(sourceFd);
			return false;
		}

#if !defined(DEATH_TARGET_APPLE) && !defined(DEATH_TARGET_SWITCH) && !defined(__FreeBSD__)
		while (true) {
			if (::fallocate(destFd, FALLOC_FL_KEEP_SIZE, 0, sb.st_size) == 0) {
				break;
			}
			std::int32_t error = errno;
			if (error == EOPNOTSUPP || error == ENOSYS) {
				break;
			}
			if (error != EINTR) {
				return false;
			}
		}
#endif

#	if defined(DEATH_TARGET_APPLE)
		// fcopyfile works on FreeBSD and OS X 10.5+ 
		bool success = (::fcopyfile(sourceFd, destFd, 0, COPYFILE_ALL) == 0);
#	elif defined(__linux__)
		constexpr std::size_t MaxSendSize = 0x7FFFF000u;

		std::uint64_t size = sb.st_size;
		std::uint64_t offset = 0;
		bool success = true;
		while (offset < size) {
			std::uint64_t bytesLeft = size - offset;
			std::size_t bytesToCopy = MaxSendSize;
			if (bytesLeft < static_cast<std::uint64_t>(MaxSendSize)) {
				bytesToCopy = static_cast<std::size_t>(bytesLeft);
			}
			ssize_t sz = ::sendfile(destFd, sourceFd, nullptr, bytesToCopy);
			if DEATH_UNLIKELY(sz < 0) {
				std::int32_t error = errno;
				if (error == EINTR) {
					continue;
				}

				success = false;
				break;
			}

			offset += sz;
		}
#	else
#		if defined(POSIX_FADV_SEQUENTIAL) && (!defined(__ANDROID__) || __ANDROID_API__ >= 21) && !defined(DEATH_TARGET_SWITCH)
		// As noted in https://eklitzke.org/efficient-file-copying-on-linux, might make the file reading faster
		::posix_fadvise(sourceFd, 0, 0, POSIX_FADV_SEQUENTIAL);
#		endif

#		if defined(DEATH_TARGET_EMSCRIPTEN)
		constexpr std::size_t BufferSize = 8 * 1024;
#		else
		constexpr std::size_t BufferSize = 128 * 1024;
#		endif
		char buffer[BufferSize];
		bool success = true;
		while (true) {
			ssize_t bytesRead = ::read(sourceFd, buffer, BufferSize);
			if (bytesRead == 0) {
				break;
			}
			if DEATH_UNLIKELY(bytesRead < 0) {
				if (errno == EINTR) {
					continue;	// Retry
				}

				success = false;
				goto End;
			}

			ssize_t bytesWritten = 0;
			do {
				ssize_t sz = ::write(destFd, buffer + bytesWritten, bytesRead);
				if DEATH_UNLIKELY(sz < 0) {
					if (errno == EINTR) {
						continue;	// Retry
					}

					success = false;
					goto End;
				}

				bytesRead -= sz;
				bytesWritten += sz;
			} while (bytesRead > 0);
		}
	End:
#	endif

#	if !defined(DEATH_TARGET_EMSCRIPTEN)
		// If we created a new file with an explicitly added S_IWUSR permission, we may need to update its mode bits to match the source file.
		if (destMode != sourceMode && ::fchmod(destFd, sourceMode) != 0) {
			success = false;
		}
#	endif

		::close(sourceFd);
		::close(destFd);

		return success;
#endif
	}

	std::int64_t FileSystem::GetFileSize(const StringView path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS)
#	if defined(DEATH_TARGET_WINDOWS_RT)
		HANDLE hFile = ::CreateFileFromAppW(Utf8::ToUtf16(path), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#	else
		HANDLE hFile = ::CreateFileW(Utf8::ToUtf16(path), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#	endif
		LARGE_INTEGER fileSize;
		fileSize.QuadPart = 0;
		const BOOL status = ::GetFileSizeEx(hFile, &fileSize);
		::CloseHandle(hFile);
		return (status != 0 ? static_cast<std::int64_t>(fileSize.QuadPart) : -1);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath.data())) {
			return AndroidAssetStream::GetFileSize(nullTerminatedPath.data());
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) != 0) {
			return -1;
		}
		return static_cast<std::int64_t>(sb.st_size);
#endif
	}

	DateTime FileSystem::GetCreationTime(const StringView path)
	{
		if (path.empty()) return { };

		DateTime date;
#if defined(DEATH_TARGET_WINDOWS)
#	if defined(DEATH_TARGET_WINDOWS_RT)
		HANDLE hFile = ::CreateFileFromAppW(Utf8::ToUtf16(path), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#	else
		HANDLE hFile = ::CreateFileW(Utf8::ToUtf16(path), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#	endif
		FILETIME fileTime;
		if (::GetFileTime(hFile, &fileTime, nullptr, nullptr)) {
			date = DateTime(fileTime);
		}
		::CloseHandle(hFile);
#elif defined(DEATH_TARGET_APPLE) && defined(_DARWIN_FEATURE_64_BIT_INODE)
		auto nullTerminatedPath = String::nullTerminatedView(path);
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) == 0) {
			date = DateTime(sb.st_birthtimespec.tv_sec);
			date.SetMillisecond(sb.st_birthtimespec.tv_nsec / 1000000);
		}
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath.data())) {
			return date;
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) == 0) {
			// Creation time is not available on Linux, return the last change of inode instead
			date = DateTime(sb.st_ctime);
		}
#endif
		return date;
	}

	DateTime FileSystem::GetLastModificationTime(const StringView path)
	{
		if (path.empty()) return { };

		DateTime date;
#if defined(DEATH_TARGET_WINDOWS)
#	if defined(DEATH_TARGET_WINDOWS_RT)
		HANDLE hFile = ::CreateFileFromAppW(Utf8::ToUtf16(path), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#	else
		HANDLE hFile = ::CreateFileW(Utf8::ToUtf16(path), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#	endif
		FILETIME fileTime;
		if (::GetFileTime(hFile, nullptr, nullptr, &fileTime)) {
			date = DateTime(fileTime);
		}
		::CloseHandle(hFile);
#elif defined(DEATH_TARGET_APPLE) && defined(_DARWIN_FEATURE_64_BIT_INODE)
		auto nullTerminatedPath = String::nullTerminatedView(path);
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) == 0) {
			date = DateTime(sb.st_mtimespec.tv_sec);
			date.SetMillisecond(sb.st_mtimespec.tv_nsec / 1000000);
		}
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath.data())) {
			return date;
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) == 0) {
			date = DateTime(sb.st_mtime);
		}
#endif
		return date;
	}

	DateTime FileSystem::GetLastAccessTime(const StringView path)
	{
		if (path.empty()) return { };

		DateTime date;
#if defined(DEATH_TARGET_WINDOWS)
#	if defined(DEATH_TARGET_WINDOWS_RT)
		HANDLE hFile = ::CreateFileFromAppW(Utf8::ToUtf16(path), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#	else
		HANDLE hFile = ::CreateFileW(Utf8::ToUtf16(path), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#	endif
		FILETIME fileTime;
		if (::GetFileTime(hFile, nullptr, &fileTime, nullptr)) {
			date = DateTime(fileTime);
		}
		::CloseHandle(hFile);
#elif defined(DEATH_TARGET_APPLE) && defined(_DARWIN_FEATURE_64_BIT_INODE)
		auto nullTerminatedPath = String::nullTerminatedView(path);
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) == 0) {
			date = DateTime(sb.st_atimespec.tv_sec);
			date.SetMillisecond(sb.st_atimespec.tv_nsec / 1000000);
		}
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath.data())) {
			return date;
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) == 0) {
			date = DateTime(sb.st_atime);
		}
#endif
		return date;
	}

	FileSystem::Permission FileSystem::GetPermissions(const StringView path)
	{
		if (path.empty()) return Permission::None;

#if defined(DEATH_TARGET_WINDOWS)
		Permission mode = Permission::Read;
		if (IsExecutable(path)) {
			mode |= Permission::Execute;
		}
		const DWORD attrs = ::GetFileAttributesW(Utf8::ToUtf16(path));
		if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY) == 0) {
			mode |= Permission::Write;
		}
		return mode;
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath.data())) {
			if (AndroidAssetStream::TryOpenDirectory(nullTerminatedPath.data())) {
				return (Permission::Read | Permission::Execute);
			} else if (AndroidAssetStream::TryOpenFile(nullTerminatedPath.data())) {
				return Permission::Read;
			}
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) != 0) {
			return Permission::None;
		}
		return NativeModeToEnum(sb.st_mode);
#endif
	}

	bool FileSystem::ChangePermissions(const StringView path, Permission mode)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS)
		DWORD attrs = ::GetFileAttributesW(Utf8::ToUtf16(path));
		if (attrs != INVALID_FILE_ATTRIBUTES) {
			if ((mode & Permission::Write) == Permission::Write && (attrs & FILE_ATTRIBUTE_READONLY) == FILE_ATTRIBUTE_READONLY) {
				// Adding the write permission
				attrs &= ~FILE_ATTRIBUTE_READONLY;
				return ::SetFileAttributesW(Utf8::ToUtf16(path), attrs);
			} else if ((mode & Permission::Write) != Permission::Write && (attrs & FILE_ATTRIBUTE_READONLY) != FILE_ATTRIBUTE_READONLY) {
				// Removing the write permission
				attrs |= FILE_ATTRIBUTE_READONLY;
				return ::SetFileAttributesW(Utf8::ToUtf16(path), attrs);
			}
			return true;
		}
		return false;
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath.data())) {
			return false;
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) != 0) {
			return false;
		}
		const std::uint32_t currentMode = sb.st_mode;
		std::uint32_t newMode = AddPermissionsToCurrent(currentMode & ~(S_IRUSR | S_IWUSR | S_IXUSR), mode);
		return (::chmod(nullTerminatedPath.data(), newMode) == 0);
#endif
	}

	bool FileSystem::AddPermissions(const StringView path, Permission mode)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS)
		DWORD attrs = ::GetFileAttributesW(Utf8::ToUtf16(path));
		if (attrs != INVALID_FILE_ATTRIBUTES) {
			// Adding the write permission
			if ((mode & Permission::Write) == Permission::Write && (attrs & FILE_ATTRIBUTE_READONLY) == FILE_ATTRIBUTE_READONLY) {
				attrs &= ~FILE_ATTRIBUTE_READONLY;
				return ::SetFileAttributesW(Utf8::ToUtf16(path), attrs);
			}
			return true;
		}
		return false;
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath.data())) {
			return false;
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) != 0) {
			return false;
		}
		const std::uint32_t currentMode = sb.st_mode;
		const std::uint32_t newMode = AddPermissionsToCurrent(currentMode, mode);
		return (::chmod(nullTerminatedPath.data(), newMode) == 0);
#endif
	}

	bool FileSystem::RemovePermissions(const StringView path, Permission mode)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS)
		DWORD attrs = ::GetFileAttributesW(Utf8::ToUtf16(path));
		if (attrs != INVALID_FILE_ATTRIBUTES) {
			// Removing the write permission
			if ((mode & Permission::Write) == Permission::Write && (attrs & FILE_ATTRIBUTE_READONLY) != FILE_ATTRIBUTE_READONLY) {
				attrs |= FILE_ATTRIBUTE_READONLY;
				return ::SetFileAttributesW(Utf8::ToUtf16(path), attrs);
			}
			return true;
		}
		return false;
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AndroidAssetStream::TryGetAssetPath(nullTerminatedPath.data())) {
			return false;
		}
#	endif
		struct stat sb;
		if (::stat(nullTerminatedPath.data(), &sb) != 0) {
			return false;
		}
		const std::uint32_t currentMode = sb.st_mode;
		const std::uint32_t newMode = RemovePermissionsFromCurrent(currentMode, mode);
		return (::chmod(nullTerminatedPath.data(), newMode) == 0);
#endif
	}

	bool FileSystem::LaunchDirectoryAsync(const StringView path)
	{
#if defined(DEATH_TARGET_APPLE)
		Class nsStringClass = objc_getClass("NSString");
		Class nsUrlClass = objc_getClass("NSURL");
		Class nsWorkspaceClass = objc_getClass("NSWorkspace");
		if (nsStringClass != nullptr && nsUrlClass != nullptr && nsWorkspaceClass != nullptr) {
			id pathString = ((id(*)(Class, SEL, const char*))objc_msgSend)(nsStringClass, sel_getUid("stringWithUTF8String:"), String::nullTerminatedView(path).data());
			id pathUrl = ((id(*)(Class, SEL, id))objc_msgSend)(nsUrlClass, sel_getUid("fileURLWithPath:"), pathString);
			id workspaceInstance = ((id(*)(Class, SEL))objc_msgSend)(nsWorkspaceClass, sel_getUid("sharedWorkspace"));
			return ((bool(*)(id, SEL, id))objc_msgSend)(workspaceInstance, sel_getUid("openURL:"), pathUrl);
		}
		return false;
#elif defined(DEATH_TARGET_WINDOWS_RT)
		if (!DirectoryExists(path)) {
			return false;
		}
		Array<wchar_t> nullTerminatedPath = Utf8::ToUtf16(path);
		winrt::Windows::System::Launcher::LaunchFolderPathAsync(winrt::hstring(nullTerminatedPath.data(), (winrt::hstring::size_type)(nullTerminatedPath.size() - 1)));
		return true;
#elif defined(DEATH_TARGET_WINDOWS)
		if (!DirectoryExists(path)) {
			return false;
		}
		return (INT_PTR)::ShellExecuteW(NULL, nullptr, Utf8::ToUtf16(path), nullptr, nullptr, SW_SHOWNORMAL) > 32;
#elif defined(DEATH_TARGET_UNIX)
		if (!DirectoryExists(path)) {
			return false;
		}

		pid_t child = ::fork();
		if (child < 0) {
			return false;
		}
		if (child == 0) {
			pid_t doubleFork = ::fork();
			if (doubleFork < 0) {
				_exit(1);
			}
			if (doubleFork == 0) {
				TryCloseAllFileDescriptors();

				RedirectFileDescriptorToNull(STDIN_FILENO);
				RedirectFileDescriptorToNull(STDOUT_FILENO);
				RedirectFileDescriptorToNull(STDERR_FILENO);

				// Execute child process in a new process group
				::setsid();

				// Execute "xdg-open"
				::execlp("xdg-open", "xdg-open", String::nullTerminatedView(path).data(), (char*)0);
				_exit(1);
			} else {
				_exit(0);
			}
		}

		std::int32_t status;
		::waitpid(child, &status, 0);
		return (WEXITSTATUS(status) == 0);
#else
		return false;
#endif
	}

#if defined(DEATH_TARGET_EMSCRIPTEN)
	void FileSystem::MountAsPersistent(const StringView path)
	{
		// It's calling asynchronous API synchronously, so it can block main thread for a while
		std::int32_t result = __asyncjs__MountAsPersistent(path.data(), path.size());
		if (!result) {
			LOGW("MountAsPersistent(\"%s\") failed", String::nullTerminatedView(path).data());
		}
	}

	void FileSystem::SyncToPersistent()
	{
		EM_ASM({
			FS.syncfs(false, function(err) {
				// Don't wait for completion, it should take ~1 second, so it doesn't matter
			});
		});
	}
#endif

	std::unique_ptr<Stream> FileSystem::Open(const String& path, FileAccess mode)
	{
#if defined(DEATH_TARGET_ANDROID)
		StringView assetName = AndroidAssetStream::TryGetAssetPath(String::nullTerminatedView(path).data());
		if (!assetName.empty()) {
			return std::make_unique<AndroidAssetStream>(assetName, mode);
		}
#endif
		return std::make_unique<FileStream>(path, mode);
	}

#if defined(DEATH_TARGET_UNIX) || (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT))
	void FileSystem::MapDeleter::operator()(const char* const data, const std::size_t size)
	{
#	if defined(DEATH_TARGET_UNIX)
		if (data != nullptr) ::munmap(const_cast<char*>(data), size);
		if (_fd != 0) ::close(_fd);
#	elif defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		if (data != nullptr) ::UnmapViewOfFile(data);
		if (_hMap != nullptr) ::CloseHandle(_hMap);
		if (_hFile != NULL) ::CloseHandle(_hFile);
		static_cast<void>(size);
#	endif
	}

	std::optional<Array<char, FileSystem::MapDeleter>> FileSystem::OpenAsMemoryMapped(const StringView path, FileAccess mode)
	{
#	if defined(DEATH_TARGET_UNIX)
		int flags, prot;
		switch (mode) {
			case FileAccess::Read:
				flags = O_RDONLY;
				prot = PROT_READ;
				break;
			case FileAccess::ReadWrite:
				flags = O_RDWR;
				prot = PROT_READ | PROT_WRITE;
				break;
			default:
				LOGE("Cannot open file \"%s\" - Invalid mode (%u)", String::nullTerminatedView(path).data(), (std::uint32_t)mode);
				return { };
		}

		const std::int32_t fd = ::open(String::nullTerminatedView(path).data(), flags);
		if (fd == -1) {
			LOGE("Cannot open file \"%s\"", String::nullTerminatedView(path).data());
			return { };
		}

		// Explicitly fail if opening directories for reading on Unix to prevent silent errors
		struct stat sb;
		if (::fstat(fd, &sb) == 0 && S_ISDIR(sb.st_mode)) {
			LOGE("Cannot open file \"%s\"", String::nullTerminatedView(path).data());
			::close(fd);
			return { };
		}

		const off_t currentPos = ::lseek(fd, 0, SEEK_CUR);
		const std::size_t size = ::lseek(fd, 0, SEEK_END);
		::lseek(fd, currentPos, SEEK_SET);

		// Can't call mmap() with a zero size, so if the file is empty just set the pointer to null - but for consistency keep
		// the fd open and let it be handled by the deleter. Array guarantees that deleter gets called even in case of a null data.
		char* data;
		if (size == 0) {
			data = nullptr;
		} else if ((data = reinterpret_cast<char*>(::mmap(nullptr, size, prot, MAP_SHARED, fd, 0))) == MAP_FAILED) {
			::close(fd);
			return { };
		}

		return Array<char, MapDeleter>{ data, size, MapDeleter { fd }};
#	elif defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		DWORD fileDesiredAccess, shareMode, protect, mapDesiredAccess;
		switch (mode) {
			case FileAccess::Read:
				fileDesiredAccess = GENERIC_READ;
				shareMode = FILE_SHARE_READ;
				protect = PAGE_READONLY;
				mapDesiredAccess = FILE_MAP_READ;
				break;
			case FileAccess::ReadWrite:
				fileDesiredAccess = GENERIC_READ | GENERIC_WRITE;
				shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
				protect = PAGE_READWRITE;
				mapDesiredAccess = FILE_MAP_ALL_ACCESS;
				break;
			default:
				LOGE("Cannot open file \"%s\" - Invalid mode (%u)", String::nullTerminatedView(path).data(), (std::uint32_t)mode);
				return { };
		}

		HANDLE hFile = ::CreateFileW(Utf8::ToUtf16(path), fileDesiredAccess, shareMode, nullptr, OPEN_EXISTING, 0, nullptr);
		if (hFile == INVALID_HANDLE_VALUE) {
			DWORD error = ::GetLastError();
			LOGE("Cannot open file \"%s\" with error 0x%08x%s", String::nullTerminatedView(path).data(), error, __GetWin32ErrorSuffix(error));
			return { };
		}

		const std::size_t size = ::GetFileSize(hFile, nullptr);

		// Can't call CreateFileMapping() with a zero size, so if the file is empty just set the pointer to null -- but for consistency keep
		// the handle open and let it be handled by the deleter. Array guarantees that deleter gets called even in case of a null data.
		HANDLE hMap;
		char* data;
		if (size == 0) {
			hMap = { };
			data = nullptr;
		} else {
			if (!(hMap = ::CreateFileMappingW(hFile, nullptr, protect, 0, 0, nullptr))) {
				DWORD error = ::GetLastError();
				LOGE("Cannot open file \"%s\" with error 0x%08x%s", String::nullTerminatedView(path).data(), error, __GetWin32ErrorSuffix(error));
				::CloseHandle(hFile);
				return { };
			}

			if (!(data = reinterpret_cast<char*>(::MapViewOfFile(hMap, mapDesiredAccess, 0, 0, 0)))) {
				DWORD error = ::GetLastError();
				LOGE("Cannot open file \"%s\" with error 0x%08x%s", String::nullTerminatedView(path).data(), error, __GetWin32ErrorSuffix(error));
				::CloseHandle(hMap);
				::CloseHandle(hFile);
				return { };
			}
		}

		return Containers::Array<char, MapDeleter>{ data, size, MapDeleter { hFile, hMap }};
#	endif
	}
#endif

	std::unique_ptr<Stream> FileSystem::CreateFromMemory(std::uint8_t* bufferPtr, std::int32_t bufferSize)
	{
		DEATH_ASSERT(bufferPtr != nullptr, "bufferPtr is null", nullptr);
		DEATH_ASSERT(bufferSize > 0, "bufferSize is 0", nullptr);
		return std::make_unique<MemoryStream>(bufferPtr, bufferSize);
	}

	std::unique_ptr<Stream> FileSystem::CreateFromMemory(const std::uint8_t* bufferPtr, std::int32_t bufferSize)
	{
		DEATH_ASSERT(bufferPtr != nullptr, "bufferPtr is null", nullptr);
		DEATH_ASSERT(bufferSize > 0, "bufferSize is 0", nullptr);
		return std::make_unique<MemoryStream>(bufferPtr, bufferSize);
	}

	const String& FileSystem::GetSavePath(const StringView applicationName)
	{
		if (_savePath.empty()) {
			InitializeSavePath(applicationName);
		}
		return _savePath;
	}

	void FileSystem::InitializeSavePath(const StringView applicationName)
	{
#if defined(DEATH_TARGET_ANDROID)
		_savePath = AndroidAssetStream::GetInternalDataPath();
		if (!DirectoryExists(_savePath)) {
			// Trying to create the data directory
			if (!CreateDirectories(_savePath)) {
				_savePath = { };
			}
		}
#elif defined(DEATH_TARGET_APPLE)
		// Not delegating into GetHomeDirectory() as the (admittedly rare) error message would have a confusing source
		StringView home = ::getenv("HOME");
		if (home.empty()) {
			return;
		}

		_savePath = CombinePath({ home, "Library/Application Support"_s, applicationName });
#elif defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_EMSCRIPTEN)
		StringView config = ::getenv("XDG_CONFIG_HOME");
		if (IsAbsolutePath(config)) {
			_savePath = CombinePath(config, applicationName);
			return;
		}

		// Not delegating into GetHomeDirectory() as the (admittedly rare) error message would have a confusing source
		StringView home = ::getenv("HOME");
		if (home.empty()) {
			return;
		}

		_savePath = CombinePath({ home, ".config"_s, applicationName });
#elif defined(DEATH_TARGET_WINDOWS_RT)
		auto appData = winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path();
		_savePath = Death::Utf8::FromUtf16(appData.data(), appData.size());
#elif defined(DEATH_TARGET_WINDOWS)
		wchar_t* path = nullptr;
		bool success = (::SHGetKnownFolderPath(FOLDERID_SavedGames, KF_FLAG_DEFAULT, nullptr, &path) == S_OK);
		if (!success || path == nullptr || path[0] == L'\0') {
			if (path != nullptr) {
				::CoTaskMemFree(path);
				path = nullptr;
			}

			success = (::SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &path) == S_OK);
		}
		if (success && path != nullptr && path[0] != L'\0') {
			_savePath = CombinePath(Utf8::FromUtf16(path), applicationName);
		}
		if (path != nullptr) {
			::CoTaskMemFree(path);
		}
#endif
	}
}}
