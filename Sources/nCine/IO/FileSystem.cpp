#include "FileSystem.h"
#include "MemoryFile.h"
#include "StandardFile.h"
#include "../Base/Algorithms.h"

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
#	include <time.h>
#	include <ftw.h>
#
#	if defined(DEATH_TARGET_UNIX)
#		include <sys/wait.h>
#	endif
#	if defined(__linux__)
#		include <sys/sendfile.h>
#	endif
#
#	if defined(DEATH_TARGET_ANDROID)
#		include "../Backends/Android/AndroidApplication.h"
#		include "AssetFile.h"
#	endif
#endif

#if defined(DEATH_TARGET_APPLE)
#	include <mach-o/dyld.h>
#endif

#if defined(DEATH_TARGET_EMSCRIPTEN)
#	include <emscripten/emscripten.h>
#endif

#if defined(DEATH_TARGET_WINDOWS_RT)
#	include <winrt/Windows.Foundation.h>
#	include <winrt/Windows.Storage.h>
#	include <winrt/Windows.System.h>
#	include <winrt/Windows.UI.Core.h>
#	include "../Backends/Uwp/UwpApplication.h"
#endif

#include <Environment.h>
#include <Utf8.h>
#include <Containers/String.h>
#include <Containers/GrowableArray.h>

using namespace Death;
using namespace Death::Containers;

namespace nCine
{
	namespace
	{
		static char buffer[FileSystem::MaxPathLength];

		static size_t GetPathRootLength(const StringView& path)
		{
			if (path.empty()) return 0;

#if defined(DEATH_TARGET_WINDOWS)
			constexpr StringView ExtendedPathPrefix = "\\\\?\\"_s;
			constexpr StringView UncExtendedPathPrefix = "\\\\?\\UNC\\"_s;

			size_t i = 0;
			size_t pathLength = path.size();
			size_t volumeSeparatorLength = 2;  // Length to the colon "C:"
			size_t uncRootLength = 2;          // Length to the start of the server name "\\"

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
					int n = 2;	// Maximum separators to skip
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
			return (path[0] == '/' || path[0] == '\\' ? 1 : 0);
#endif
		}

#if defined(DEATH_TARGET_WINDOWS)
		static FileSystem::FileDate NativeTimeToFileDate(const SYSTEMTIME* sysTime, int64_t ticks)
		{
			FileSystem::FileDate date = { };

			date.Year = sysTime->wYear;
			date.Month = sysTime->wMonth;
			date.Day = sysTime->wDay;
			date.Hour = sysTime->wHour;
			date.Minute = sysTime->wMinute;
			date.Second = sysTime->wSecond;
			date.Ticks = ticks;

			return date;
		}

		static bool DeleteDirectoryInternal(const ArrayView<wchar_t>& path, bool recursive, int depth)
		{
			if (recursive) {
				if (path.size() + 3 <= fs::MaxPathLength) {
					auto bufferExtended = Array<wchar_t>(NoInit, fs::MaxPathLength);
					std::memcpy(bufferExtended.data(), path.data(), path.size() * sizeof(wchar_t));

					size_t bufferOffset = path.size();
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
					HANDLE hFindFile = ::FindFirstFileExFromAppW(bufferExtended, FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
#	else
					HANDLE hFindFile = ::FindFirstFileEx(bufferExtended, Environment::IsWindows7() ? FindExInfoBasic : FindExInfoStandard, &data, FindExSearchNameMatch, nullptr, 0);
#	endif
					if (hFindFile != NULL && hFindFile != INVALID_HANDLE_VALUE) {
						do {
							if (data.cFileName[0] == L'.' && (data.cFileName[1] == L'\0' || (data.cFileName[1] == L'.' && data.cFileName[2] == L'\0'))) {
								continue;
							}

							size_t fileNameLength = wcslen(data.cFileName);
							std::memcpy(&bufferExtended[bufferOffset], data.cFileName, fileNameLength * sizeof(wchar_t));

							if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY) {
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
										if (!::DeleteVolumeMountPoint(bufferExtended)) {
											// Cannot unmount this mount point
										}
									}

									// RemoveDirectory on a symbolic link will remove the link itself
#	if defined(DEATH_TARGET_WINDOWS_RT)
									if (!::RemoveDirectoryFromAppW(bufferExtended)) {
#	else
									if (!::RemoveDirectory(bufferExtended)) {
#	endif
										DWORD err = ::GetLastError();
										if (err != ERROR_PATH_NOT_FOUND) {
											// Cannot remove symbolic link
										}
									}
								}
							} else {
								bufferExtended[bufferOffset + fileNameLength] = L'\0';

#	if defined(DEATH_TARGET_WINDOWS_RT)
								::DeleteFileFromAppW(bufferExtended);
#	else
								::DeleteFile(bufferExtended);
#	endif
							}
						} while (::FindNextFile(hFindFile, &data));

						::FindClose(hFindFile);
					}
				}
			}

#	if defined(DEATH_TARGET_WINDOWS_RT)
			return ::RemoveDirectoryFromAppW(path);
#	else
			return ::RemoveDirectory(path);
#	endif
		}
#else
		static bool CallStat(const char* path, struct stat& sb)
		{
			ASSERT(path);
			if (::lstat(path, &sb) == -1) {
				LOGD_X("lstat(\"%s\") failed: %s", path, strerror(errno));
				return false;
			}
			return true;
		}

		static FileSystem::Permission NativeModeToEnum(unsigned int nativeMode)
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

		static unsigned int AddPermissionsToCurrent(unsigned int currentMode, FileSystem::Permission mode)
		{
			if ((mode & FileSystem::Permission::Read) == FileSystem::Permission::Read)
				currentMode |= S_IRUSR;
			if ((mode & FileSystem::Permission::Write) == FileSystem::Permission::Write)
				currentMode |= S_IWUSR;
			if ((mode & FileSystem::Permission::Execute) == FileSystem::Permission::Execute)
				currentMode |= S_IXUSR;

			return currentMode;
		}

		static unsigned int RemovePermissionsFromCurrent(unsigned int currentMode, FileSystem::Permission mode)
		{
			if ((mode & FileSystem::Permission::Read) == FileSystem::Permission::Read)
				currentMode &= ~S_IRUSR;
			if ((mode & FileSystem::Permission::Write) == FileSystem::Permission::Write)
				currentMode &= ~S_IWUSR;
			if ((mode & FileSystem::Permission::Execute) == FileSystem::Permission::Execute)
				currentMode &= ~S_IXUSR;

			return currentMode;
		}

		static FileSystem::FileDate NativeTimeToFileDate(const time_t* t)
		{
			FileSystem::FileDate date = { };

			struct tm* local = localtime(t);
			date.Year = local->tm_year + 1900;
			date.Month = local->tm_mon + 1;
			date.Day = local->tm_mday;
			date.Hour = local->tm_hour;
			date.Minute = local->tm_min;
			date.Second = local->tm_sec;
			date.Ticks = static_cast<int64_t>(*t);

			return date;
		}

		static int DeleteDirectoryInternalCallback(const char* fpath, const struct stat* sb, int typeflag, struct FTW* ftwbuf)
		{
			return ::remove(fpath);
		}

		static bool DeleteDirectoryInternal(const StringView& path)
		{
			return ::nftw(String::nullTerminatedView(path).data(), DeleteDirectoryInternalCallback, 64, FTW_DEPTH | FTW_PHYS) == 0;
		}

#	if defined(DEATH_TARGET_UNIX)
		static bool RedirectFileDescriptorToNull(int fd)
		{
			if (fd == -1) {
				return false;
			}

			int tempfd;
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
			DIR* dir = ::opendir("/proc/self/fd/");
			if (dir == nullptr) {
				const long fd_max = ::sysconf(_SC_OPEN_MAX);
				long fd;
				for (fd = 0; fd <= fd_max; fd++) {
					::close(fd);
				}
				return;
			}

			int dfd = ::dirfd(dir);
			struct dirent* ent;
			while ((ent = ::readdir(dir))) {
				if (ent->d_name[0] >= '0' && ent->d_name[0] <= '9') {
					const char* p = &ent->d_name[1];
					int fd = ent->d_name[0] - '0';
					while (*p >= '0' && *p <= '9') {
						fd = (10 * fd) + *(p++) - '0';
					}
					if (*p || fd == dfd) {
						continue;
					}
					::close(fd);
				}
			}
			::closedir(dir);
		}
#	endif
#endif

#if defined(DEATH_TARGET_EMSCRIPTEN)
		EM_JS(int, __asyncjs__MountAsPersistent, (const char* path, int pathLength), {
			return Asyncify.handleSleep(function(callback) {
				var p = UTF8ToString(path, pathLength);

				FS.mkdir(p);
				FS.mount(IDBFS, { }, p);

				FS.syncfs(true, function(err) {
					callback(err ? 0 : 1);
				});
			});
		});
#endif
	}

	///////////////////////////////////////////////////////////
	// STATIC DEFINITIONS
	///////////////////////////////////////////////////////////

	String FileSystem::_dataPath;
	String FileSystem::_savePath;

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	FileSystem::Directory::Directory(const StringView& path, EnumerationOptions options)
	{
		Open(path, options);
	}

	FileSystem::Directory::~Directory()
	{
		Close();
	}

	bool FileSystem::Directory::Open(const StringView& path, EnumerationOptions options)
	{
		Close();

		_options = options;
#if defined(DEATH_TARGET_WINDOWS)
		_path[0] = '\0';
		if (!path.empty() && IsDirectory(path)) {
			_firstFile = true;

			// Prepare full path to found files
			{
				String absPath = GetAbsolutePath(path);
				size_t pathLength = absPath.size();
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

			Array<wchar_t> buffer = Utf8::ToUtf16(_path, (int)(_fileNamePart - _path));
			if (buffer.size() + 2 <= MaxPathLength) {
				auto bufferExtended = Array<wchar_t>(NoInit, buffer.size() + 2);
				std::memcpy(bufferExtended.data(), buffer.data(), buffer.size() * sizeof(wchar_t));

				// Adding a wildcard to list all files in the directory
				bufferExtended[buffer.size()] = L'*';
				bufferExtended[buffer.size() + 1] = L'\0';

				WIN32_FIND_DATA data;
#	if defined(DEATH_TARGET_WINDOWS_RT)
				_hFindFile = ::FindFirstFileExFromAppW(bufferExtended, FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
#	else
				_hFindFile = ::FindFirstFileEx(bufferExtended, Environment::IsWindows7() ? FindExInfoBasic : FindExInfoStandard, &data, FindExSearchNameMatch, nullptr, 0);
#	endif
				if (_hFindFile) {
					if ((data.cFileName[0] == L'.' && (data.cFileName[1] == L'\0' || (data.cFileName[1] == L'.' && data.cFileName[2] == L'\0'))) ||
						((_options & EnumerationOptions::SkipDirectories) == EnumerationOptions::SkipDirectories && (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY) ||
						((_options & EnumerationOptions::SkipFiles) == EnumerationOptions::SkipFiles && (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY)) {
						_firstFile = false;
					} else {
						strncpy_s(_fileNamePart, sizeof(_path) - (_fileNamePart - _path), Utf8::FromUtf16(data.cFileName).data(), MaxPathLength - 1);
					}
				}
			}
		}
		return (_hFindFile != NULL && _hFindFile != INVALID_HANDLE_VALUE);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		const char* assetPath = AssetFile::TryGetAssetPath(nullTerminatedPath.data());
		if (assetPath != nullptr) {
			// It probably supports only files
			if ((_options & EnumerationOptions::SkipFiles) == EnumerationOptions::SkipFiles) {
				return false;
			}
			_assetDir = AssetFile::OpenDir(assetPath);
			if (_assetDir != nullptr) {
				size_t pathLength = path.size();
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
		} else
#	endif
		if (!nullTerminatedPath.empty()) {
			_dirStream = ::opendir(nullTerminatedPath.data());
			if (_dirStream != nullptr) {
				String absPath = GetAbsolutePath(path);
				size_t pathLength = absPath.size();
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

	void FileSystem::Directory::Close()
	{
#if defined(DEATH_TARGET_WINDOWS)
		if (_hFindFile != NULL && _hFindFile != INVALID_HANDLE_VALUE) {
			::FindClose(_hFindFile);
			_hFindFile = NULL;
		}
#else
#	if defined(DEATH_TARGET_ANDROID)
		if (_assetDir != nullptr) {
			AssetFile::CloseDir(_assetDir);
			_assetDir = nullptr;
		} else
#	endif
		if (_dirStream != nullptr) {
			::closedir(_dirStream);
			_dirStream = nullptr;
		}
#endif
	}

	const char* FileSystem::Directory::GetNext()
	{
#if defined(DEATH_TARGET_WINDOWS)
		if (_hFindFile == NULL || _hFindFile == INVALID_HANDLE_VALUE) {
			return nullptr;
		}

		if (_firstFile) {
			_firstFile = false;
			return _path;
		} else {
		Retry:
			WIN32_FIND_DATA data;
			if (::FindNextFile(_hFindFile, &data)) {
				if ((data.cFileName[0] == L'.' && (data.cFileName[1] == L'\0' || (data.cFileName[1] == L'.' && data.cFileName[2] == L'\0'))) ||
					((_options & EnumerationOptions::SkipDirectories) == EnumerationOptions::SkipDirectories && (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY) ||
					((_options & EnumerationOptions::SkipFiles) == EnumerationOptions::SkipFiles && (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY)) {
					goto Retry;
				} else {
					strncpy_s(_fileNamePart, sizeof(_path) - (_fileNamePart - _path), Utf8::FromUtf16(data.cFileName).data(), MaxPathLength - 1);
				}
				return _path;
			}
			return nullptr;
		}
#else
#	if defined(DEATH_TARGET_ANDROID)
		// It does not return directory names
		if (_assetDir != nullptr) {
			const char* assetName = AssetFile::GetNextFileName(_assetDir);
			if (assetName == nullptr) {
				return nullptr;
			}
			strcpy(_fileNamePart, assetName);
			return _path;
		}
#	endif
		if (_dirStream == nullptr) {
			return nullptr;
		}

		struct dirent* entry;
	Retry:
		entry = ::readdir(_dirStream);
		if (entry) {
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
			size_t charsLeft = sizeof(_path) - (_fileNamePart - _path) - 1;
			size_t fileLength = strlen(entry->d_name);
			if (fileLength > charsLeft) {
				return nullptr;
			}
			strcpy(_fileNamePart, entry->d_name);
			return _path;
		} else {
			return nullptr;
		}
#endif
	}

#if !defined(DEATH_TARGET_WINDOWS)
	String FileSystem::FindPathCaseInsensitive(const StringView& path)
	{
		if (Exists(path)) {
			return path;
		}

		size_t l = path.size();
		char* p = (char*)alloca(l + 1);
		strncpy(p, path.data(), l);
		p[l] = '\0';
		size_t rl = 0;
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

	String FileSystem::JoinPath(const StringView& first, const StringView& second)
	{
		size_t firstSize = first.size();
		size_t secondSize = second.size();

		if (firstSize == 0 || (secondSize != 0 && (second[0] == '/' || second[0] == '\\'
#if defined(DEATH_TARGET_WINDOWS)
			// Absolute filename on Windows
			|| (secondSize > 2 && second[1] == ':' && (second[2] == '/' || second[2] == '\\'))
#endif
			))) {
			return second;
		}

		if (secondSize == 0) {
			return first;
		}

#	if defined(DEATH_TARGET_ANDROID)
		if (first == AssetFile::Prefix) {
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

	String FileSystem::JoinPath(const ArrayView<const StringView> paths)
	{
		if (paths.empty()) return { };

		size_t count = paths.size();
		size_t resultSize = 0;
		size_t startIdx = 0;
		for (size_t i = 0; i < count; i++) {
			size_t pathSize = paths[i].size();
			if (pathSize == 0) {
				continue;
			}
			if (paths[i][0] == '/' || paths[i][0] == '\\') {
				resultSize = 0;
				startIdx = i;
			}

			resultSize += pathSize;

			if (i + 1 < count && paths[i][pathSize - 1] != '/' && paths[i][pathSize - 1] != '\\') {
				resultSize++;
			}
		}

		String result(NoInit, resultSize);
		resultSize = 0;
		for (size_t i = startIdx; i < count; i++) {
			size_t pathSize = paths[i].size();
			if (pathSize == 0) {
				continue;
			}

			memcpy(&result[resultSize], paths[i].data(), pathSize);
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

	String FileSystem::JoinPath(const std::initializer_list<StringView> paths)
	{
		return JoinPath(Containers::arrayView(paths));
	}

	String FileSystem::JoinPathAbsolute(const StringView& first, const StringView& second)
	{
		String returnedPath = JoinPath(first, second);
#if defined(DEATH_TARGET_WINDOWS)
		wchar_t buffer[MaxPathLength];
		const wchar_t* resolvedPath = _wfullpath(buffer, Utf8::ToUtf16(returnedPath), countof(buffer));
		if (resolvedPath == nullptr) {
			return { };
		}
		return Utf8::FromUtf16(buffer);
#else
#	if defined(DEATH_TARGET_ANDROID)
		if (returnedPath.hasPrefix(AssetFile::Prefix)) {
			return returnedPath;
		}
#	endif

		const char* resolvedPath = ::realpath(returnedPath.data(), buffer);
		if (resolvedPath == nullptr) {
			buffer[0] = '\0';
		}
		return buffer;
#endif
	}

	StringView FileSystem::GetDirectoryName(const StringView& path)
	{
		if (path.empty()) return { };

		size_t pathRootLength = GetPathRootLength(path);
		size_t i = path.size();
		// Strip any trailing path separators
		while (i > pathRootLength && (path[i - 1] == '/' || path[i - 1] == '\\')) {
			i--;
		}
		if (i <= pathRootLength) return { };
		// Try to get the last path separator
		while (i > pathRootLength && path[--i] != '/' && path[i] != '\\');
		// Return nothing if only filename was specified as relative path
		if (pathRootLength == 0 && i == 0) {
#if defined(DEATH_TARGET_ANDROID)
			if (path != AssetFile::Prefix && path.hasPrefix(AssetFile::Prefix)) {
				return AssetFile::Prefix;
			}
#endif
			return { };
		}

		return path.slice(0, i);
	}

	StringView FileSystem::GetFileName(const StringView& path)
	{
		if (path.empty()) return { };

		size_t pathRootLength = GetPathRootLength(path);
		size_t pathLength = path.size();
		// Strip any trailing path separators
		while (pathLength > pathRootLength && (path[pathLength - 1] == '/' || path[pathLength - 1] == '\\')) {
			pathLength--;
		}
		if (pathLength <= pathRootLength) return { };
		size_t i = pathLength;
		// Try to get the last path separator
		while (i > pathRootLength && path[--i] != '/' && path[i] != '\\');

		if (path[i] == '/' || path[i] == '\\') {
			i++;
		}
		return path.slice(i, pathLength);
	}

	StringView FileSystem::GetFileNameWithoutExtension(const StringView& path)
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

	String FileSystem::GetExtension(const StringView& path)
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
		lowercaseInPlace(result);
		return result;
	}

#if defined(DEATH_TARGET_WINDOWS)
	String FileSystem::ToNativeSeparators(String path)
	{
		// Take ownership first if not already (e.g., directly from `String::nullTerminatedView()`)
		if (!path.isSmall() && path.deleter()) {
			path = String { path };
		}
		for (char& c : path) {
			if (c == '/') {
				c = '\\';
			}
		}
		return path;
	}
#endif

	String FileSystem::GetAbsolutePath(const StringView& path)
	{
		if (path.empty()) return { };

#if defined(DEATH_TARGET_WINDOWS)
		wchar_t buffer[MaxPathLength];
		const wchar_t* resolvedPath = _wfullpath(buffer, Utf8::ToUtf16(path), countof(buffer));
		if (resolvedPath == nullptr) {
			return { };
		}
		return Utf8::FromUtf16(buffer);
#else
#	if defined(DEATH_TARGET_ANDROID)
		if (path.hasPrefix(AssetFile::Prefix)) {
			return path;
		}
#	endif

		const char* resolvedPath = ::realpath(String::nullTerminatedView(path).data(), buffer);
		if (resolvedPath == nullptr) {
			return { };
		}
		return buffer;
#endif
	}

	String FileSystem::GetExecutablePath()
	{
#if defined(DEATH_TARGET_EMSCRIPTEN)
		return "/"_s;
#elif defined(DEATH_TARGET_APPLE)
		// Get path size (need to set it to 0 to avoid filling nullptr with random data and crashing)
		uint32_t size = 0;
		if (_NSGetExecutablePath(nullptr, &size) != -1) {
			return { };
		}

		// Allocate proper size and get the path. The size includes a null terminator which the String handles on its own, so subtract it
		String path { NoInit, size - 1 };
		if (_NSGetExecutablePath(path.data(), &size) != 0) {
			return { };
		}
		return path;
#elif defined(DEATH_TARGET_UNIX)
		// Reallocate like hell until we have enough place to store the path. Can't use lstat because
		// the /proc/self/exe symlink is not a real symlink and so stat::st_size returns 0.
		constexpr const char self[] = "/proc/self/exe";
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
		return String { path.release(), size_t(size), deleter };

#elif defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		wchar_t path[MaxPathLength];
		// Returns size *without* the null terminator
		const size_t size = ::GetModuleFileName(nullptr, path, countof(path));
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
		::GetCurrentDirectory(MaxPathLength, buffer);
		return Utf8::FromUtf16(buffer);
#else
		if (::getcwd(buffer, MaxPathLength) != nullptr) {
			return buffer;
		} else {
			return { };
		}
#endif
	}

	bool FileSystem::SetWorkingDirectory(const StringView& path)
	{
#if defined(DEATH_TARGET_EMSCRIPTEN)
		return false;
#elif defined(DEATH_TARGET_WINDOWS)
		return ::SetCurrentDirectory(Utf8::ToUtf16(path));
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
		::SHGetFolderPath(HWND_DESKTOP, CSIDL_PROFILE, NULL, SHGFP_TYPE_CURRENT, buffer);
		return Utf8::FromUtf16(buffer);
#else
		const char* home = ::getenv("HOME");
		if (home != nullptr && home[0] != '\0') {
			return home;
		} else {
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
			// `getpwuid()` is not yet implemented on Emscripten
			const struct passwd* pw = ::getpwuid(getuid());
			if (pw) {
				return pw->pw_dir;
			}
#	endif
			return { };
		}
#endif
	}

#if defined(DEATH_TARGET_ANDROID)
	String FileSystem::GetExternalStorage()
	{
		const char* extStorage = ::getenv("EXTERNAL_STORAGE");
		if (extStorage == nullptr || extStorage[0] == '\0') {
			return String { "/sdcard"_s };
		}
		return extStorage;
	}
#elif defined(DEATH_TARGET_UNIX)
	String FileSystem::GetLocalStorage()
	{
		const char* localStorage = ::getenv("XDG_DATA_HOME");
		if (localStorage != nullptr && localStorage[0] != '\0') {
			return localStorage;
		}

		// Not delegating into GetHomeDirectory() as the (admittedly rare) error message would have a confusing source
		const char* home = ::getenv("HOME");
		if (home != nullptr && home[0] != '\0') {
			return JoinPath(home, ".local/share/"_s);
		}

		return { };
	}
#endif

	bool FileSystem::IsDirectory(const StringView& path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return (::GetFileAttributesExFromAppW(Utf8::ToUtf16(path), GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY);
#elif defined(DEATH_TARGET_WINDOWS)
		const DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AssetFile::TryGetAssetPath(nullTerminatedPath.data())) {
			return AssetFile::TryOpenDirectory(nullTerminatedPath.data());
		}
#	endif

		struct stat sb;
		if (CallStat(nullTerminatedPath.data(), sb)) {
			return (sb.st_mode & S_IFMT) == S_IFDIR;
		}
		return false;
#endif
	}

	bool FileSystem::IsFile(const StringView& path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return (::GetFileAttributesExFromAppW(Utf8::ToUtf16(path), GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY);
#elif defined(DEATH_TARGET_WINDOWS)
		const DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AssetFile::TryGetAssetPath(nullTerminatedPath.data())) {
			return AssetFile::TryOpenFile(nullTerminatedPath.data());
		}
#	endif

		struct stat sb;
		if (CallStat(nullTerminatedPath.data(), sb)) {
			return (sb.st_mode & S_IFMT) == S_IFREG;
		}
		return false;
#endif
	}

	bool FileSystem::Exists(const StringView& path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return !(!::GetFileAttributesExFromAppW(Utf8::ToUtf16(path), GetFileExInfoStandard, &lpFileInfo) && ::GetLastError() == ERROR_FILE_NOT_FOUND);
#elif defined(DEATH_TARGET_WINDOWS)
		const DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		return !(attrs == INVALID_FILE_ATTRIBUTES && ::GetLastError() == ERROR_FILE_NOT_FOUND);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AssetFile::TryGetAssetPath(nullTerminatedPath.data())) {
			return AssetFile::TryOpen(nullTerminatedPath.data());
		}
#	endif

		struct stat sb;
		return CallStat(nullTerminatedPath.data(), sb);
#endif
	}

	bool FileSystem::IsReadable(const StringView& path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return ::GetFileAttributesExFromAppW(Utf8::ToUtf16(path), GetFileExInfoStandard, &lpFileInfo);
#elif defined(DEATH_TARGET_WINDOWS)
		const DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		return (attrs != INVALID_FILE_ATTRIBUTES);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AssetFile::TryGetAssetPath(nullTerminatedPath.data())) {
			return AssetFile::TryOpen(nullTerminatedPath.data());
		}
#	endif

		struct stat sb;
		if (CallStat(nullTerminatedPath.data(), sb)) {
			return (sb.st_mode & S_IRUSR);
		}
		return false;
#endif
	}

	bool FileSystem::IsWritable(const StringView& path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return (::GetFileAttributesExFromAppW(Utf8::ToUtf16(path), GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_READONLY) == 0);
#elif defined(DEATH_TARGET_WINDOWS)
		const DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY) == 0);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AssetFile::TryGetAssetPath(nullTerminatedPath.data())) {
			return false;
		}
#	endif

		struct stat sb;
		if (CallStat(nullTerminatedPath.data(), sb)) {
			return (sb.st_mode & S_IWUSR);
		}
		return false;
#endif
	}

	bool FileSystem::IsExecutable(const StringView& path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		return false;
#elif defined(DEATH_TARGET_WINDOWS)
		// Assuming that every file that exists is also executable
		const DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
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
		if (AssetFile::TryGetAssetPath(nullTerminatedPath.data())) {
			return AssetFile::TryOpenDirectory(nullTerminatedPath.data());
		}
#	endif

		return (::access(nullTerminatedPath.data(), X_OK) == 0);
#endif
	}

	bool FileSystem::IsReadableFile(const StringView& path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return (::GetFileAttributesExFromAppW(Utf8::ToUtf16(path), GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY);
#elif defined(DEATH_TARGET_WINDOWS)
		const DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != FILE_ATTRIBUTE_DIRECTORY);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AssetFile::TryGetAssetPath(nullTerminatedPath.data())) {
			return AssetFile::TryOpenFile(nullTerminatedPath.data());
		}
#	endif

		struct stat sb;
		if (CallStat(nullTerminatedPath.data(), sb)) {
			return (sb.st_mode & S_IFMT) == S_IFREG && (sb.st_mode & S_IRUSR);
		}
#endif
		return false;
	}

	bool FileSystem::IsWritableFile(const StringView& path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return (::GetFileAttributesExFromAppW(Utf8::ToUtf16(path), GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY)) == 0);
#elif defined(DEATH_TARGET_WINDOWS)
		const DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY)) == 0);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AssetFile::TryGetAssetPath(nullTerminatedPath.data())) {
			return false;
		}
#	endif

		struct stat sb;
		if (CallStat(nullTerminatedPath.data(), sb)) {
			return (sb.st_mode & S_IFMT) == S_IFREG && (sb.st_mode & S_IWUSR);
		}
#endif
		return false;
	}

	bool FileSystem::IsHidden(const StringView& path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		return (::GetFileAttributesExFromAppW(Utf8::ToUtf16(path), GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) == FILE_ATTRIBUTE_HIDDEN);
#elif defined(DEATH_TARGET_WINDOWS)
		const DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_HIDDEN) == FILE_ATTRIBUTE_HIDDEN);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AssetFile::TryGetAssetPath(nullTerminatedPath.data())) {
			return false;
		}
#	endif

		size_t pathLength = std::min((size_t)MaxPathLength - 1, path.size());
		strncpy(buffer, path.data(), pathLength);
		buffer[pathLength] = '\0';
		const char* baseName = ::basename(buffer);
		return (baseName && baseName[0] == '.');
#endif
	}

	bool FileSystem::SetHidden(const StringView& path, bool hidden)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		Array<wchar_t> nullTerminatedPath = Utf8::ToUtf16(path);
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		if (!::GetFileAttributesExFromAppW(nullTerminatedPath, GetFileExInfoStandard, &lpFileInfo)) {
			return true;
		}

		if (hidden && (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != FILE_ATTRIBUTE_HIDDEN) {
			// Adding the hidden flag
			lpFileInfo.dwFileAttributes |= FILE_ATTRIBUTE_HIDDEN;
			return ::SetFileAttributes(nullTerminatedPath, lpFileInfo.dwFileAttributes);
		} else if (!hidden && (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) == FILE_ATTRIBUTE_HIDDEN) {
			// Removing the hidden flag
			lpFileInfo.dwFileAttributes &= ~FILE_ATTRIBUTE_HIDDEN;
			return ::SetFileAttributes(nullTerminatedPath, lpFileInfo.dwFileAttributes);
		}
#elif defined(DEATH_TARGET_WINDOWS)
		Array<wchar_t> nullTerminatedPath = Utf8::ToUtf16(path);
		DWORD attrs = ::GetFileAttributes(nullTerminatedPath);
		if (attrs == INVALID_FILE_ATTRIBUTES) {
			return false;
		}

		if (hidden && (attrs & FILE_ATTRIBUTE_HIDDEN) != FILE_ATTRIBUTE_HIDDEN) {
			// Adding the hidden flag
			attrs |= FILE_ATTRIBUTE_HIDDEN;
			return ::SetFileAttributes(nullTerminatedPath, attrs);
		} else if (!hidden && (attrs & FILE_ATTRIBUTE_HIDDEN) == FILE_ATTRIBUTE_HIDDEN) {
			// Removing the hidden flag
			attrs &= ~FILE_ATTRIBUTE_HIDDEN;
			return ::SetFileAttributes(nullTerminatedPath, attrs);
		}
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AssetFile::TryGetAssetPath(nullTerminatedPath.data())) {
			return false;
		}
#	endif

		size_t pathLength = std::min((size_t)MaxPathLength - 1, path.size());
		strncpy(buffer, nullTerminatedPath.data(), pathLength);
		buffer[pathLength] = '\0';
		const char* baseName = ::basename(buffer);
		if (hidden && baseName && baseName[0] != '.') {
			String newPath = JoinPath(GetDirectoryName(nullTerminatedPath), "."_s + baseName);
			return (::rename(nullTerminatedPath.data(), newPath.data()) == 0);
		} else if (!hidden && baseName && baseName[0] == '.') {
			int numDots = 0;
			while (baseName[numDots] == '.') {
				numDots++;
			}
			String newPath = JoinPath(GetDirectoryName(nullTerminatedPath), &buffer[numDots]);
			return (::rename(nullTerminatedPath.data(), newPath.data()) == 0);
		}
#endif
		return false;
	}

	bool FileSystem::CreateDirectories(const StringView& path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS)
		Array<wchar_t> fullPath = Utf8::ToUtf16(path);
		// Don't use IsDirectory() to avoid calling Utf8::ToUtf16() twice
#	if defined(DEATH_TARGET_WINDOWS_RT)
		WIN32_FILE_ATTRIBUTE_DATA lpFileInfo;
		if (::GetFileAttributesExFromAppW(fullPath, GetFileExInfoStandard, &lpFileInfo) && (lpFileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY) {
			return true;
		}
#	else
		const DWORD attrs = ::GetFileAttributes(fullPath);
		if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY) {
			return true;
		}
#	endif

		int fullPathSize = (int)fullPath.size();
		int startIdx = 0;
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
		for (int i = startIdx; i < fullPathSize; i++) {
			if (fullPath[i] == L'\0') {
				break;
			}

			if (fullPath[i] == L'/' || fullPath[i] == L'\\') {
				fullPath[i] = L'\0';
#	if defined(DEATH_TARGET_WINDOWS_RT)
				if (!::GetFileAttributesExFromAppW(fullPath, GetFileExInfoStandard, &lpFileInfo)) {
					if (!::CreateDirectoryFromAppW(fullPath, NULL)) {
#	else
				const DWORD attrs = ::GetFileAttributes(fullPath);
				if (attrs == INVALID_FILE_ATTRIBUTES) {
					if (!::CreateDirectory(fullPath, NULL)) {
#	endif
						DWORD err = ::GetLastError();
						if (err != ERROR_ALREADY_EXISTS) {
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
			if (!::CreateDirectory(fullPath, NULL)) {
#	endif
				DWORD err = ::GetLastError();
				if (err != ERROR_ALREADY_EXISTS) {
					return false;
				}
			}
		}
		return true;
#else
		if (IsDirectory(path)) {
			return true;
		}

		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AssetFile::TryGetAssetPath(nullTerminatedPath.data())) {
			return false;
		}
#	endif

		String fullPath = String { nullTerminatedPath };
		bool slashWasLast = true;
		struct stat sb;
		for (int i = 0; i < fullPath.size(); i++) {
			if (fullPath[i] == '\0') {
				break;
			}

			if (fullPath[i] == '/' || fullPath[i] == '\\') {
				if (i > 0) {
					fullPath[i] = '\0';
					if (!CallStat(fullPath.data(), sb)) {
						if (::mkdir(fullPath.data(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0 && errno != EEXIST) {
							LOGW_X("Cannot create directory \"%s\"", fullPath.data());
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
				LOGW_X("Cannot create directory \"%s\"", fullPath.data());
				return false;
			}
		}
		return true;
#endif
	}

	bool FileSystem::RemoveDirectoryRecursive(const StringView& path)
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
		const DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
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

	bool FileSystem::RemoveFile(const StringView& path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		return ::DeleteFileFromAppW(Utf8::ToUtf16(path));
#elif defined(DEATH_TARGET_WINDOWS)
		return ::DeleteFile(Utf8::ToUtf16(path));
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AssetFile::TryGetAssetPath(nullTerminatedPath.data())) {
			return false;
		}
#	endif

		return (::unlink(nullTerminatedPath.data()) == 0);
#endif
	}

	bool FileSystem::Rename(const StringView& oldPath, const StringView& newPath)
	{
		if (oldPath.empty() || newPath.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS)
		return ::MoveFileEx(Utf8::ToUtf16(oldPath), Utf8::ToUtf16(newPath), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
#else
		auto nullTerminatedOldPath = String::nullTerminatedView(oldPath);
		auto nullTerminatedNewPath = String::nullTerminatedView(newPath);
#	if defined(DEATH_TARGET_ANDROID)
		if (AssetFile::TryGetAssetPath(nullTerminatedOldPath.data())) {
			return false;
		}
#	endif

		return (::rename(nullTerminatedOldPath.data(), nullTerminatedNewPath.data()) == 0);
#endif
	}

	bool FileSystem::Copy(const StringView& oldPath, const StringView& newPath, bool overwrite)
	{
		if (oldPath.empty() || newPath.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS_RT)
		return ::CopyFileFromAppW(Utf8::ToUtf16(oldPath), Utf8::ToUtf16(newPath), overwrite ? TRUE : FALSE);
#elif defined(DEATH_TARGET_WINDOWS)
		return ::CopyFile(Utf8::ToUtf16(oldPath), Utf8::ToUtf16(newPath), overwrite ? TRUE : FALSE);
#elif defined(__linux__)
		auto nullTerminatedOldPath = String::nullTerminatedView(oldPath);
		auto nullTerminatedNewPath = String::nullTerminatedView(newPath);
#	if defined(DEATH_TARGET_ANDROID)
		if (AssetFile::TryGetAssetPath(nullTerminatedOldPath.data())) {
			return false;
		}
#	endif
		if (!overwrite && Exists(newPath)) {
			return false;
		}

		int source, dest;
		off_t bytes = 0;
		struct stat sb;

		if ((source = ::open(nullTerminatedOldPath.data(), O_RDONLY)) == -1) {
			return false;
		}
		fstat(source, &sb);
		if ((dest = ::creat(nullTerminatedNewPath.data(), sb.st_mode)) == -1) {
			::close(source);
			return false;
		}

		const int status = ::sendfile(dest, source, &bytes, sb.st_size);

		::close(source);
		::close(dest);

		return (status != -1);
#else
		if (!overwrite && Exists(newPath)) {
			return false;
		}

		constexpr size_t BufferSize = 128 * 1024;
		char buffer[BufferSize];

		int source, dest;
		size_t size = 0;
		struct stat sb;

		if ((source = ::open(String::nullTerminatedView(oldPath).data(), O_RDONLY)) == -1) {
			return false;
		}
		fstat(source, &sb);
		if ((dest = ::open(String::nullTerminatedView(newPath).data(), O_WRONLY | O_CREAT, sb.st_mode)) == -1) {
			::close(source);
			return false;
		}

#	if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L
		// As noted in https://eklitzke.org/efficient-file-copying-on-linux, might make the file reading faster
		::posix_fadvise(source, 0, 0, POSIX_FADV_SEQUENTIAL);
#	endif

		while ((size = ::read(source, buffer, BufferSize)) > 0) {
			::write(dest, buffer, size);
		}
		::close(source);
		::close(dest);

		return true;
#endif
	}

	int64_t FileSystem::FileSize(const StringView& path)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS)
#	if defined(DEATH_TARGET_WINDOWS_RT)
		HANDLE hFile = ::CreateFileFromAppW(Utf8::ToUtf16(path), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#	else
		HANDLE hFile = ::CreateFile(Utf8::ToUtf16(path), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#	endif
		LARGE_INTEGER fileSize;
		fileSize.QuadPart = 0;
		const BOOL status = ::GetFileSizeEx(hFile, &fileSize);
		::CloseHandle(hFile);
		return (status != 0 ? static_cast<int64_t>(fileSize.QuadPart) : -1);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AssetFile::TryGetAssetPath(nullTerminatedPath.data())) {
			return static_cast<int64_t>(AssetFile::GetLength(nullTerminatedPath.data()));
		}
#	endif

		struct stat sb;
		if (!CallStat(nullTerminatedPath.data(), sb)) {
			return -1;
		}
		return static_cast<int64_t>(sb.st_size);
#endif
	}

	FileSystem::FileDate FileSystem::LastModificationTime(const StringView& path)
	{
		if (path.empty()) return { };

		FileDate date = { };
#if defined(DEATH_TARGET_WINDOWS)
#	if defined(DEATH_TARGET_WINDOWS_RT)
		HANDLE hFile = ::CreateFileFromAppW(Utf8::ToUtf16(path), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#	else
		HANDLE hFile = ::CreateFile(Utf8::ToUtf16(path), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#	endif
		FILETIME fileTime;
		if (::GetFileTime(hFile, nullptr, nullptr, &fileTime)) {
			SYSTEMTIME sysTime;
			::FileTimeToSystemTime(&fileTime, &sysTime);
			date = NativeTimeToFileDate(&sysTime, *(int64_t*)&fileTime);
		}
		::CloseHandle(hFile);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AssetFile::TryGetAssetPath(nullTerminatedPath.data())) {
			return date;
		}
#	endif

		struct stat sb;
		if (CallStat(nullTerminatedPath.data(), sb)) {
			date = NativeTimeToFileDate(&sb.st_mtime);
		}
#endif

		return date;
	}

	FileSystem::FileDate FileSystem::LastAccessTime(const StringView& path)
	{
		if (path.empty()) return { };

		FileDate date = { };
#if defined(DEATH_TARGET_WINDOWS)
#	if defined(DEATH_TARGET_WINDOWS_RT)
		HANDLE hFile = ::CreateFileFromAppW(Utf8::ToUtf16(path), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#	else
		HANDLE hFile = ::CreateFile(Utf8::ToUtf16(path), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
#	endif
		FILETIME fileTime;
		if (::GetFileTime(hFile, nullptr, &fileTime, nullptr)) {
			SYSTEMTIME sysTime;
			::FileTimeToSystemTime(&fileTime, &sysTime);
			date = NativeTimeToFileDate(&sysTime, *(int64_t*)&fileTime);
		}
		::CloseHandle(hFile);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AssetFile::TryGetAssetPath(nullTerminatedPath.data())) {
			return date;
		}
#	endif

		struct stat sb;
		if (CallStat(nullTerminatedPath.data(), sb)) {
			date = NativeTimeToFileDate(&sb.st_atime);
		}
#endif

		return date;
	}

	FileSystem::Permission FileSystem::GetPermissions(const StringView& path)
	{
		if (path.empty()) return Permission::None;

#if defined(DEATH_TARGET_WINDOWS)
		Permission mode = Permission::Read;
		if (IsExecutable(path)) {
			mode |= Permission::Execute;
		}
		const DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY) == 0) {
			mode |= Permission::Write;
		}
		return mode;
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AssetFile::TryGetAssetPath(nullTerminatedPath.data())) {
			if (AssetFile::TryOpenDirectory(nullTerminatedPath.data())) {
				return (Permission::Read | Permission::Execute);
			} else if (AssetFile::TryOpenFile(nullTerminatedPath.data())) {
				return Permission::Read;
			}
		}
#	endif

		struct stat sb;
		if (!CallStat(nullTerminatedPath.data(), sb)) {
			return Permission::None;
		}
		return NativeModeToEnum(sb.st_mode);
#endif
	}

	bool FileSystem::ChangePermissions(const StringView& path, Permission mode)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS)
		DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		if (attrs != INVALID_FILE_ATTRIBUTES) {
			if ((mode & Permission::Write) == Permission::Write && (attrs & FILE_ATTRIBUTE_READONLY)) {
				// Adding the write permission
				attrs &= ~FILE_ATTRIBUTE_READONLY;
				return ::SetFileAttributes(Utf8::ToUtf16(path), attrs);
			} else if ((mode & Permission::Write) == Permission::Write == 0 && (attrs & FILE_ATTRIBUTE_READONLY) == 0) {
				// Removing the write permission
				attrs |= FILE_ATTRIBUTE_READONLY;
				return ::SetFileAttributes(Utf8::ToUtf16(path), attrs);
			}
			return true;
		}
		return false;
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AssetFile::TryGetAssetPath(nullTerminatedPath.data())) {
			return false;
		}
#	endif

		struct stat sb;
		if (!CallStat(nullTerminatedPath.data(), sb)) {
			return false;
		}
		const unsigned int currentMode = sb.st_mode;
		unsigned int newMode = AddPermissionsToCurrent(currentMode & ~(S_IRUSR | S_IWUSR | S_IXUSR), mode);
		return (::chmod(nullTerminatedPath.data(), newMode) == 0);
#endif
	}

	bool FileSystem::AddPermissions(const StringView& path, Permission mode)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS)
		DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		if (attrs != INVALID_FILE_ATTRIBUTES) {
			// Adding the write permission
			if ((mode & Permission::Write) == Permission::Write && (attrs & FILE_ATTRIBUTE_READONLY)) {
				attrs &= ~FILE_ATTRIBUTE_READONLY;
				return ::SetFileAttributes(Utf8::ToUtf16(path), attrs);
			}
			return true;
		}
		return false;
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AssetFile::TryGetAssetPath(nullTerminatedPath.data())) {
			return false;
		}
#	endif

		struct stat sb;
		if (!CallStat(nullTerminatedPath.data(), sb)) {
			return false;
		}
		const unsigned int currentMode = sb.st_mode;
		const unsigned int newMode = AddPermissionsToCurrent(currentMode, mode);
		return (::chmod(nullTerminatedPath.data(), newMode) == 0);
#endif
	}

	bool FileSystem::RemovePermissions(const StringView& path, Permission mode)
	{
		if (path.empty()) return false;

#if defined(DEATH_TARGET_WINDOWS)
		DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		if (attrs != INVALID_FILE_ATTRIBUTES) {
			// Removing the write permission
			if ((mode & Permission::Write) == Permission::Write && (attrs & FILE_ATTRIBUTE_READONLY) == 0) {
				attrs |= FILE_ATTRIBUTE_READONLY;
				return ::SetFileAttributes(Utf8::ToUtf16(path), attrs);
			}
			return true;
		}
		return false;
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#	if defined(DEATH_TARGET_ANDROID)
		if (AssetFile::TryGetAssetPath(nullTerminatedPath.data())) {
			return false;
		}
#	endif

		struct stat sb;
		if (!CallStat(nullTerminatedPath.data(), sb)) {
			return false;
		}
		const unsigned int currentMode = sb.st_mode;
		const unsigned int newMode = RemovePermissionsFromCurrent(currentMode, mode);
		return (::chmod(nullTerminatedPath.data(), newMode) == 0);
#endif
	}

	bool FileSystem::LaunchDirectoryAsync(const StringView& path)
	{
#if defined(DEATH_TARGET_EMSCRIPTEN)
		return false;
#elif defined(DEATH_TARGET_WINDOWS_RT)
		if (!IsDirectory(path)) {
			return false;
		}
		Array<wchar_t> nullTerminatedPath = Utf8::ToUtf16(path);
		//UwpApplication::GetDispatcher().RunIdleAsync([nullTerminatedPath = std::move(nullTerminatedPath)](auto args) {
			winrt::Windows::System::Launcher::LaunchFolderPathAsync(winrt::hstring(nullTerminatedPath.data(), (winrt::hstring::size_type)(nullTerminatedPath.size() - 1)));
		//});
		return true;
#elif defined(DEATH_TARGET_WINDOWS)
		if (!IsDirectory(path)) {
			return false;
		}
		return (INT_PTR)::ShellExecute(NULL, nullptr, Utf8::ToUtf16(path), nullptr, nullptr, SW_SHOWNORMAL) > 32;
#elif defined(DEATH_TARGET_UNIX)
		if (!IsDirectory(path)) {
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

		int status;
		::waitpid(child, &status, 0);
		return (WEXITSTATUS(status) == 0);
#else
		return false;
#endif
	}

#if defined(DEATH_TARGET_EMSCRIPTEN)
	void FileSystem::MountAsPersistent(const StringView& path)
	{
		// It's calling asynchronous API synchronously, so it can block main thread for a while
		int result = __asyncjs__MountAsPersistent(path.data(), path.size());
		if (!result) {
			LOGW_X("MountAsPersistent(\"%s\") failed", String::nullTerminatedView(path).data());
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

	std::unique_ptr<IFileStream> FileSystem::Open(const String& path, FileAccessMode mode)
	{
		std::unique_ptr<IFileStream> stream;
#if defined(DEATH_TARGET_ANDROID)
		const char* assetFilename = AssetFile::TryGetAssetPath(String::nullTerminatedView(path).data());
		if (assetFilename) {
			stream = std::make_unique<AssetFile>(assetFilename);
		} else
#endif
		stream = std::make_unique<StandardFile>(path);

		if (mode != FileAccessMode::None) {
			stream->Open(mode);
		}

		return stream;
	}

	std::unique_ptr<IFileStream> FileSystem::CreateFromMemory(unsigned char* bufferPtr, uint32_t bufferSize)
	{
		ASSERT(bufferPtr);
		ASSERT(bufferSize > 0);
		return std::make_unique<MemoryFile>(bufferPtr, bufferSize);
	}

	std::unique_ptr<IFileStream> FileSystem::CreateFromMemory(const unsigned char* bufferPtr, uint32_t bufferSize)
	{
		ASSERT(bufferPtr);
		ASSERT(bufferSize > 0);
		return std::make_unique<MemoryFile>(bufferPtr, bufferSize);
	}

	const String& FileSystem::GetSavePath(const StringView& applicationName)
	{
		if (_savePath.empty()) {
			InitializeSavePath(applicationName);
		}
		return _savePath;
	}

	///////////////////////////////////////////////////////////
	// PRIVATE FUNCTIONS
	///////////////////////////////////////////////////////////

	void FileSystem::InitializeSavePath(const StringView& applicationName)
	{
#if defined(DEATH_TARGET_ANDROID)
		AndroidApplication& application = static_cast<AndroidApplication&>(theApplication());

		// Get the internal data path from the Android application
		_savePath = application.internalDataPath();

		if (!IsDirectory(_savePath)) {
			// Trying to create the data directory
			if (!CreateDirectories(_savePath)) {
				LOGE_X("Cannot create directory: %s", _savePath.data());
				_savePath = { };
			}
		}
#elif defined(DEATH_TARGET_APPLE)
		// Not delegating into GetHomeDirectory() as the (admittedly rare) error message would have a confusing source
		const char* home = ::getenv("HOME");
		if (home == nullptr) {
			return;
		}

		_savePath = JoinPath({ home, "Library/Application Support"_s, applicationName });
#elif defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_EMSCRIPTEN)
		// XDG-compliant Unix (not using DEATH_TARGET_UNIX, because that is a superset), Emscripten
		const char* config = ::getenv("XDG_CONFIG_HOME");
		if (config != nullptr && config[0] != '\0') {
			_savePath = JoinPath(config, applicationName);
			return;
		}

		// Not delegating into GetHomeDirectory() as the (admittedly rare) error message would have a confusing source
		const char* home = ::getenv("HOME");
		if (home == nullptr) {
			return;
		}

		_savePath = JoinPath({ home, ".config"_s, applicationName });
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
			_savePath = JoinPath(Utf8::FromUtf16(path), applicationName);
		}
		if (path != nullptr) {
			::CoTaskMemFree(path);
		}
#endif
	}
}
