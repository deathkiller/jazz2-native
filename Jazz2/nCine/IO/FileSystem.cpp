#include "FileSystem.h"

#include <Utf8.h>
#include <Containers/String.h>

#if defined(DEATH_TARGET_WINDOWS)
#	include <fileapi.h>
#	include <Shlobj.h>
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
#
#	if defined(__linux__)
#		include <sys/sendfile.h>
#	endif
#
#	ifdef DEATH_TARGET_ANDROID
#		include "AndroidApplication.h"
#		include "AssetFile.h"
#	endif
#endif

using namespace Death;
using namespace Death::Containers;

namespace nCine
{
	namespace
	{
		char buffer[fs::MaxPathLength];

#ifndef DEATH_TARGET_WINDOWS
		bool callStat(const char* path, struct stat& sb)
		{
			ASSERT(path);
			if (::lstat(path, &sb) == -1) {
				LOGD_X("lstat error: %s", strerror(errno));
				return false;
			}
			return true;
		}

		int nativeModeToEnum(unsigned int nativeMode)
		{
			int mode = 0;

			if (nativeMode & S_IRUSR)
				mode += FileSystem::Permission::READ;
			if (nativeMode & S_IWUSR)
				mode += FileSystem::Permission::WRITE;
			if (nativeMode & S_IXUSR)
				mode += FileSystem::Permission::EXECUTE;

			return mode;
		}

		unsigned int addPermissionsToCurrent(unsigned int currentMode, int mode)
		{
			if (mode & FileSystem::Permission::READ)
				currentMode |= S_IRUSR;
			if (mode & FileSystem::Permission::WRITE)
				currentMode |= S_IWUSR;
			if (mode & FileSystem::Permission::EXECUTE)
				currentMode |= S_IXUSR;

			return currentMode;
		}

		unsigned int removePermissionsFromCurrent(unsigned int currentMode, int mode)
		{
			if (mode & FileSystem::Permission::READ)
				currentMode &= ~S_IRUSR;
			if (mode & FileSystem::Permission::WRITE)
				currentMode &= ~S_IWUSR;
			if (mode & FileSystem::Permission::EXECUTE)
				currentMode &= ~S_IXUSR;

			return currentMode;
		}

		FileSystem::FileDate nativeTimeToFileDate(const time_t* timer)
		{
			FileSystem::FileDate date = { };

			struct tm* local = localtime(timer);
			date.Year = local->tm_year + 1900;
			date.Month = local->tm_mon + 1;
			date.Day = local->tm_mday;
			date.Hour = local->tm_hour;
			date.Minute = local->tm_min;
			date.Second = local->tm_sec;
			date.Ticks = *(int64_t*)timer;

			return date;
		}
#else
		FileSystem::FileDate nativeTimeToFileDate(const SYSTEMTIME* sysTime, int64_t ticks)
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
#endif

	}

	///////////////////////////////////////////////////////////
	// STATIC DEFINITIONS
	///////////////////////////////////////////////////////////

	String FileSystem::dataPath_;
	String FileSystem::savePath_;

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	FileSystem::Directory::Directory(const StringView& path)
	{
		open(path);
	}

	FileSystem::Directory::~Directory()
	{
		close();
	}

	bool FileSystem::Directory::open(const StringView& path)
	{
		close();

#ifdef DEATH_TARGET_WINDOWS
		path_[0] = '\0';
		if (!path.empty() && isDirectory(path)) {
			firstFile_ = true;

			String absPath = GetAbsolutePath(path);
			memcpy(path_, absPath.data(), absPath.size());
			if (path_[absPath.size() - 1] == '\\') {
				path_[absPath.size()] = '\0';
				fileNamePart_ = path_ + absPath.size();
			} else {
				path_[absPath.size()] = '\\';
				path_[absPath.size() + 1] = '\0';
				fileNamePart_ = path_ + absPath.size() + 1;
			}

			Array<wchar_t> buffer = Utf8::ToUtf16(absPath);
			if (buffer.size() + 2 <= MaxPathLength) {
				auto bufferExtended = Array<wchar_t>(NoInit, buffer.size() + 3);
				memcpy(bufferExtended.data(), buffer.data(), buffer.size() * sizeof(wchar_t));

				// Adding a wildcard to list all files in the directory
				bufferExtended[buffer.size()] = L'\\';
				bufferExtended[buffer.size() + 1] = L'*';
				bufferExtended[buffer.size() + 2] = L'\0';

				WIN32_FIND_DATA findFileData;
				hFindFile_ = ::FindFirstFile(bufferExtended, &findFileData);
				if (hFindFile_) {
					if ((findFileData.cFileName[0] == L'.' && findFileData.cFileName[1] == L'\0') ||
						(findFileData.cFileName[0] == L'.' && findFileData.cFileName[1] == L'.' && findFileData.cFileName[2] == L'\0')) {
						firstFile_ = false;
					} else {
						strncpy_s(fileNamePart_, sizeof(path_) - (fileNamePart_ - path_), Utf8::FromUtf16(findFileData.cFileName).data(), MaxPathLength - 1);
					}
				}
			}
		}
		return (hFindFile_ != NULL && hFindFile_ != INVALID_HANDLE_VALUE);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#ifdef DEATH_TARGET_ANDROID
		const char* assetPath = AssetFile::assetPath(nullTerminatedPath.data());
		if (assetPath)
			assetDir_ = AssetFile::openDir(assetPath);
		else
#endif
		if (nullTerminatedPath.empty())
			dirStream_ = opendir(nullTerminatedPath.data());
		return (dirStream_ != nullptr);
#endif
	}

	void FileSystem::Directory::close()
	{
#ifdef DEATH_TARGET_WINDOWS
		if (hFindFile_ != NULL && hFindFile_ != INVALID_HANDLE_VALUE) {
			FindClose(hFindFile_);
			hFindFile_ = NULL;
		}
#else
#ifdef DEATH_TARGET_ANDROID
		if (assetDir_) {
			AssetFile::closeDir(assetDir_);
			assetDir_ = nullptr;
		} else
#endif
			if (dirStream_) {
				closedir(dirStream_);
				dirStream_ = nullptr;
			}
#endif
	}

	const char* FileSystem::Directory::readNext()
	{
#ifdef DEATH_TARGET_WINDOWS
		if (hFindFile_ == NULL || hFindFile_ == INVALID_HANDLE_VALUE)
			return nullptr;

		if (firstFile_) {
			firstFile_ = false;
			return path_;
		} else {
		Retry:
			WIN32_FIND_DATA findFileData;
			if (::FindNextFile(hFindFile_, &findFileData)) {
				if ((findFileData.cFileName[0] == L'.' && findFileData.cFileName[1] == L'\0') ||
					(findFileData.cFileName[0] == L'.' && findFileData.cFileName[1] == L'.' && findFileData.cFileName[2] == L'\0')) {
					goto Retry;
				} else {
					strncpy_s(fileNamePart_, sizeof(path_) - (fileNamePart_ - path_), Utf8::FromUtf16(findFileData.cFileName).data(), MaxPathLength - 1);
				}
				return path_;
			}
			return nullptr;
		}
#else
#ifdef DEATH_TARGET_ANDROID
		// It does not return directory names
		if (assetDir_)
			return AssetFile::nextFileName(assetDir_);
#endif
		if (dirStream_ == nullptr)
			return nullptr;

		struct dirent* entry;
		entry = readdir(dirStream_);
		if (entry)
			return entry->d_name;
		else
			return nullptr;
#endif
	}

	String FileSystem::joinPath(const StringView& first, const StringView& second)
	{
		if (first.empty())
			return second;
		else if (second.empty())
			return first;

		const bool firstHasSeparator = first[first.size() - 1] == '/' || first[first.size() - 1] == '\\';
		const bool secondHasSeparator = second[0] == '/' || second[0] == '\\';

#ifdef DEATH_TARGET_ANDROID
		if (first == AssetFile::Prefix) {
			return first + second;
		}	
#endif

		if (firstHasSeparator != secondHasSeparator) {
			// One path has a trailing or leading separator, the other has not
			return first + second;
		} else if (!firstHasSeparator && !secondHasSeparator) {
			// Both paths have no clashing separators
#ifdef DEATH_TARGET_WINDOWS
			return "\\"_s.join({ first, second });
#else
			return "/"_s.join({ first, second });
#endif
		} else {
			// Both paths have a clashing separator, removing the leading one from the second path
			if (second.size() > 1) {
				return first + second.exceptPrefix(1);
			} else {
				return first;
			}
		}
	}

	String FileSystem::joinPath(const ArrayView<const StringView> paths)
	{
		if (paths.empty()) return { };

		// TODO: Optimize this
		Containers::String path = paths.front();
		for (std::size_t i = 1; i != paths.size(); ++i) {
			path = joinPath(path, paths[i]);
		}
		return path;
	}

	String FileSystem::joinPath(const std::initializer_list<StringView> paths)
	{
		return joinPath(Containers::arrayView(paths));
	}

	String FileSystem::absoluteJoinPath(const StringView& first, const StringView& second)
	{
		String returnedPath = joinPath(first, second);
#ifdef DEATH_TARGET_WINDOWS
		const char* resolvedPath = _fullpath(buffer, returnedPath.data(), MaxPathLength);
#else
		const char* resolvedPath = ::realpath(returnedPath.data(), buffer);
#endif
		if (resolvedPath == nullptr)
			buffer[0] = '\0';

		return buffer;
	}

	String FileSystem::GetDirectoryName(const StringView& path)
	{
		if (path.empty()) return { };

#ifdef DEATH_TARGET_WINDOWS
		static char drive[_MAX_DRIVE];
		static char dir[_MAX_DIR];

		_splitpath_s(String::nullTerminatedView(path).data(), drive, _MAX_DRIVE, dir, _MAX_DIR, nullptr, 0, nullptr, 0);

		strncpy_s(buffer, MaxPathLength, drive, _MAX_DRIVE);
		strncat_s(buffer, MaxPathLength, dir, _MAX_DIR);
		const unsigned long pathLength = strnlen(buffer, MaxPathLength);
		// If the path only contains the drive letter the trailing separator is retained
		if (pathLength > 0 && (buffer[pathLength - 1] == '/' || buffer[pathLength - 1] == '\\') &&
			(pathLength < 3 || (pathLength == 3 && buffer[1] != ':') || pathLength > 3)) {
			buffer[pathLength - 1] = '\0';
		}

		return buffer;
#else
		strncpy(buffer, path.data(), std::min((size_t)MaxPathLength - 1, path.size()));
		return ::dirname(buffer);
#endif
	}

	String FileSystem::GetFileName(const StringView& path)
	{
		if (path.empty()) return { };

#ifdef DEATH_TARGET_WINDOWS
		static char fname[_MAX_FNAME];
		static char ext[_MAX_EXT];

		_splitpath_s(String::nullTerminatedView(path).data(), nullptr, 0, nullptr, 0, fname, _MAX_FNAME, ext, _MAX_EXT);

		strncpy_s(buffer, MaxPathLength, fname, _MAX_FNAME);
		strncat_s(buffer, MaxPathLength, ext, _MAX_EXT);
		return buffer;
#else
		strncpy(buffer, path.data(), std::min((size_t)MaxPathLength - 1, path.size()));
		return ::basename(buffer);
#endif
	}

	String FileSystem::GetFileNameWithoutExtension(const StringView& path)
	{
		if (path.empty()) return { };

#ifdef DEATH_TARGET_WINDOWS
		static char fname[_MAX_FNAME];

		_splitpath_s(String::nullTerminatedView(path).data(), nullptr, 0, nullptr, 0, fname, _MAX_FNAME, nullptr, 0);

		return fname;
#else
		strncpy(buffer, path.data(), std::min((size_t)MaxPathLength - 1, path.size()));
		char* result = ::basename(buffer);
		if (result == nullptr) {
			return { };
		}

		char* lastDot = strrchr(result, '.');
		if (result < lastDot) {
			*lastDot = '\0';
		}

		return result;
#endif
	}

	String FileSystem::GetAbsolutePath(const StringView& path)
	{
		if (path.empty()) return { };

#ifdef DEATH_TARGET_WINDOWS
		const char* resolvedPath = _fullpath(buffer, String::nullTerminatedView(path).data(), MaxPathLength);
#else
		const char* resolvedPath = ::realpath(String::nullTerminatedView(path).data(), buffer);
#endif
		if (resolvedPath == nullptr)
			buffer[0] = '\0';

		return buffer;
	}

	StringView FileSystem::extension(const StringView& path)
	{
		if (path.empty()) return { };

		const StringView filename = path.suffix(path.findLastAnyOr("/\\"_s, path.begin()).end());
		const StringView foundDot = path.findLastOr('.', path.end());
		if (foundDot == path.end()) return { };

		bool initialDots = true;
		for (char i : filename.prefix(foundDot.begin())) {
			if (i != '.') {
				initialDots = false;
				break;
			}
		}
		if (initialDots) {
			return path.suffix(filename.end());
		}

		return path.suffix(foundDot.begin() + 1);
	}

	bool FileSystem::hasExtension(const StringView& path, const StringView& extension)
	{
		if (path.empty() || extension.empty()) return false;

		const StringView pathExtension = FileSystem::extension(path);
		if (!pathExtension.empty()) {
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_MINGW)
			return (_stricmp(String::nullTerminatedView(pathExtension).data(), String::nullTerminatedView(extension).data()) == 0);
#else
			return (::strcasecmp(String::nullTerminatedView(pathExtension).data(), String::nullTerminatedView(extension).data()) == 0);
#endif
		}
		return false;
	}

	String FileSystem::currentDir()
	{
#ifdef DEATH_TARGET_WINDOWS
		wchar_t buffer[MaxPathLength];
		::GetCurrentDirectory(MaxPathLength, buffer);
		return Utf8::FromUtf16(buffer);
#else
		::getcwd(buffer, MaxPathLength);
		return buffer;
#endif
	}

	bool FileSystem::setCurrentDir(const StringView& path)
	{
#ifdef DEATH_TARGET_WINDOWS
		const int status = ::SetCurrentDirectory(Utf8::ToUtf16(path));
		return (status != 0);
#else
		const int status = ::chdir(String::nullTerminatedView(path).data());
		return (status == 0);
#endif
	}

	String FileSystem::homeDir()
	{
#ifdef DEATH_TARGET_WINDOWS
		wchar_t buffer[MaxPathLength];
		::SHGetFolderPath(HWND_DESKTOP, CSIDL_PROFILE, NULL, SHGFP_TYPE_CURRENT, buffer);
		return Utf8::FromUtf16(buffer).data();
#else
		const char* homeEnv = getenv("HOME");
		if (homeEnv == nullptr || strnlen(homeEnv, MaxPathLength) == 0) {
#ifndef DEATH_TARGET_EMSCRIPTEN
			// `getpwuid()` is not yet implemented on Emscripten
			const struct passwd* pw = ::getpwuid(getuid());
			if (pw)
				return pw->pw_dir;
			else
#endif
				return currentDir();
		} else {
			return homeEnv;
		}
#endif
	}

#ifdef DEATH_TARGET_ANDROID
	String FileSystem::externalStorageDir()
	{
		const char* extStorage = getenv("EXTERNAL_STORAGE");

		if (extStorage == nullptr || extStorage[0] == '\0')
			return "/scard"_s;
		return extStorage;
	}
#endif

	bool FileSystem::isDirectory(const StringView& path)
	{
		if (path.empty()) return false;

#ifdef DEATH_TARGET_WINDOWS
		const DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		return (attrs != INVALID_FILE_ATTRIBUTES && attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#ifdef DEATH_TARGET_ANDROID
		if (AssetFile::assetPath(nullTerminatedPath.data())) {
			return AssetFile::tryOpenDirectory(nullTerminatedPath.data());
		}
#endif

		struct stat sb;
		const bool statCalled = callStat(nullTerminatedPath.data(), sb);
		if (statCalled)
			return (sb.st_mode & S_IFMT) == S_IFDIR;
		return false;
#endif
	}

	bool FileSystem::isFile(const StringView& path)
	{
		if (path == nullptr)
			return false;

#ifdef DEATH_TARGET_WINDOWS
		const DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#ifdef DEATH_TARGET_ANDROID
		if (AssetFile::assetPath(nullTerminatedPath.data()))
			return AssetFile::tryOpenFile(nullTerminatedPath.data());
#endif

		struct stat sb;
		const bool statCalled = callStat(nullTerminatedPath.data(), sb);
		if (statCalled)
			return (sb.st_mode & S_IFMT) == S_IFREG;
		return false;
#endif
	}

	bool FileSystem::exists(const StringView& path)
	{
		if (path == nullptr)
			return false;

#ifdef DEATH_TARGET_WINDOWS
		const DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		return !(attrs == INVALID_FILE_ATTRIBUTES && ::GetLastError() == ERROR_FILE_NOT_FOUND);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#ifdef DEATH_TARGET_ANDROID
		if (AssetFile::assetPath(nullTerminatedPath.data()))
			return AssetFile::tryOpen(nullTerminatedPath.data());
#endif

		struct stat sb;
		const bool statCalled = callStat(nullTerminatedPath.data(), sb);
		return statCalled;
#endif
	}

	bool FileSystem::isReadable(const StringView& path)
	{
		if (path == nullptr)
			return false;

#ifdef DEATH_TARGET_WINDOWS
		// Assuming that every file that exists is also readable
		const DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		return !(attrs == INVALID_FILE_ATTRIBUTES && ::GetLastError() == ERROR_FILE_NOT_FOUND);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#ifdef DEATH_TARGET_ANDROID
		if (AssetFile::assetPath(nullTerminatedPath.data()))
			return AssetFile::tryOpen(nullTerminatedPath.data());
#endif

		struct stat sb;
		const bool statCalled = callStat(nullTerminatedPath.data(), sb);
		if (statCalled)
			return (sb.st_mode & S_IRUSR);
		return false;
#endif
	}

	bool FileSystem::isWritable(const StringView& path)
	{
		if (path == nullptr)
			return false;

#ifdef DEATH_TARGET_WINDOWS
		const DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY) == 0);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#ifdef DEATH_TARGET_ANDROID
		if (AssetFile::assetPath(nullTerminatedPath.data()))
			return false;
#endif

		struct stat sb;
		const bool statCalled = callStat(nullTerminatedPath.data(), sb);
		if (statCalled)
			return (sb.st_mode & S_IWUSR);
		return false;
#endif
	}

	bool FileSystem::isExecutable(const StringView& path)
	{
		if (path == nullptr)
			return false;

#ifdef DEATH_TARGET_WINDOWS
		// Assuming that every file that exists is also executable
		const DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		// Assuming that every existing directory is accessible
		if (attrs != INVALID_FILE_ATTRIBUTES && attrs & FILE_ATTRIBUTE_DIRECTORY)
			return true;
		// Using some of the Windows executable extensions to detect executable files
		else if (attrs != INVALID_FILE_ATTRIBUTES &&
				 (hasExtension(path, "exe"_s) || hasExtension(path, "bat"_s) || hasExtension(path, "com"_s)))
			return true;
		else
			return false;
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#ifdef DEATH_TARGET_ANDROID
		if (AssetFile::assetPath(nullTerminatedPath.data()))
			return AssetFile::tryOpenDirectory(nullTerminatedPath.data());
#endif

		return (::access(nullTerminatedPath.data(), X_OK) == 0);
#endif
	}

	bool FileSystem::isReadableFile(const StringView& path)
	{
		if (path == nullptr)
			return false;

#ifdef DEATH_TARGET_WINDOWS
		const DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#ifdef DEATH_TARGET_ANDROID
		if (AssetFile::assetPath(nullTerminatedPath.data()))
			return AssetFile::tryOpenFile(nullTerminatedPath.data());
#endif

		struct stat sb;
		const bool statCalled = callStat(nullTerminatedPath.data(), sb);
		if (statCalled)
			return (sb.st_mode & S_IFMT) == S_IFREG && (sb.st_mode & S_IRUSR);
#endif
		return false;
	}

	bool FileSystem::isWritableFile(const StringView& path)
	{
		if (path == nullptr)
			return false;

#ifdef DEATH_TARGET_WINDOWS
		const DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		return (attrs != INVALID_FILE_ATTRIBUTES &&
				(attrs & FILE_ATTRIBUTE_READONLY) == 0 &&
				(attrs & FILE_ATTRIBUTE_DIRECTORY) == 0);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#ifdef DEATH_TARGET_ANDROID
		if (AssetFile::assetPath(nullTerminatedPath.data()))
			return false;
#endif

		struct stat sb;
		const bool statCalled = callStat(nullTerminatedPath.data(), sb);
		if (statCalled)
			return (sb.st_mode & S_IFMT) == S_IFREG && (sb.st_mode & S_IWUSR);
#endif
		return false;
	}

	bool FileSystem::isHidden(const StringView& path)
	{
		if (path == nullptr)
			return false;

#ifdef DEATH_TARGET_WINDOWS
		const DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_HIDDEN));
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#ifdef DEATH_TARGET_ANDROID
		if (AssetFile::assetPath(nullTerminatedPath.data()))
			return false;
#endif

		strncpy(buffer, path.data(), std::min((size_t)MaxPathLength - 1, path.size()));
		const char* baseName = ::basename(buffer);
		return (baseName && baseName[0] == '.');
#endif
	}

	bool FileSystem::setHidden(const StringView& path, bool hidden)
	{
		if (path == nullptr)
			return false;

#ifdef DEATH_TARGET_WINDOWS
		DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));

		// Adding the hidden flag
		if (hidden && (attrs & FILE_ATTRIBUTE_HIDDEN) == 0) {
			attrs |= FILE_ATTRIBUTE_HIDDEN;
			const int status = ::SetFileAttributes(Utf8::ToUtf16(path), attrs);
			return (status != 0);
		}
		// Removing the hidden flag
		else if (hidden == false && (attrs & FILE_ATTRIBUTE_HIDDEN)) {
			attrs &= ~FILE_ATTRIBUTE_HIDDEN;
			const int status = ::SetFileAttributes(Utf8::ToUtf16(path), attrs);
			return (status != 0);
		}
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#ifdef DEATH_TARGET_ANDROID
		if (AssetFile::assetPath(nullTerminatedPath.data()))
			return false;
#endif

		strncpy(buffer, nullTerminatedPath.data(), MaxPathLength - 1);
		const char* baseName = ::basename(buffer);
		if (hidden && baseName && baseName[0] != '.') {
			String newPath = joinPath(dirName(nullTerminatedPath), "."_s);
			newPath += baseName;
			const int status = ::rename(nullTerminatedPath.data(), newPath.data());
			return (status == 0);
		} else if (hidden == false && baseName && baseName[0] == '.') {
			int numDots = 0;
			while (baseName[numDots] == '.')
				numDots++;

			String newPath = joinPath(dirName(nullTerminatedPath), &buffer[numDots]);
			const int status = ::rename(nullTerminatedPath.data(), newPath.data());
			return (status == 0);
		}
#endif
		return false;
	}

	bool FileSystem::createDir(const StringView& path)
	{
		if (path.empty())
			return false;

		if (isDirectory(path))
			return true;

#ifdef DEATH_TARGET_WINDOWS
		Array<wchar_t> fullPath = Utf8::ToUtf16(GetAbsolutePath(path));
		bool slashWasLast = true;
		for (int i = 4; i < fullPath.size(); i++) {
			if (fullPath[i] == L'\0') {
				break;
			}

			if (fullPath[i] == L'/' || fullPath[i] == L'\\') {
				wchar_t prevChar = fullPath[i];
				fullPath[i] = L'\0';
				if (!::CreateDirectory(fullPath, NULL)) {
					DWORD err = GetLastError();
					if (err != ERROR_ALREADY_EXISTS) {
						return false;
					}
				}
				fullPath[i] = prevChar;
				slashWasLast = true;
			} else {
				slashWasLast = false;
			}
		}

		if (!slashWasLast) {
			if (!::CreateDirectory(fullPath, NULL)) {
				DWORD err = GetLastError();
				if (err != ERROR_ALREADY_EXISTS) {
					return false;
				}
			}
		}
		return true;
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#ifdef DEATH_TARGET_ANDROID
		if (AssetFile::assetPath(nullTerminatedPath.data()))
			return false;
#endif

		const int status = ::mkdir(nullTerminatedPath.data(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
		return (status == 0);
#endif
	}

	bool FileSystem::deleteEmptyDir(const StringView& path)
	{
		if (path == nullptr)
			return false;

#ifdef DEATH_TARGET_WINDOWS
		const int status = ::RemoveDirectory(Utf8::ToUtf16(path));
		return (status != 0);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#ifdef DEATH_TARGET_ANDROID
		if (AssetFile::assetPath(nullTerminatedPath.data()))
			return false;
#endif

		const int status = ::rmdir(nullTerminatedPath.data());
		return (status == 0);
#endif
	}

	bool FileSystem::deleteFile(const StringView& path)
	{
		if (path == nullptr)
			return false;

#ifdef DEATH_TARGET_WINDOWS
		const int status = ::DeleteFile(Utf8::ToUtf16(path));
		return (status != 0);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#ifdef DEATH_TARGET_ANDROID
		if (AssetFile::assetPath(nullTerminatedPath.data()))
			return false;
#endif

		const int status = ::unlink(nullTerminatedPath.data());
		return (status == 0);
#endif
	}

	bool FileSystem::rename(const StringView& oldPath, const StringView& newPath)
	{
		if (oldPath == nullptr || newPath == nullptr)
			return false;

#ifdef DEATH_TARGET_WINDOWS
		const int status = ::MoveFile(Utf8::ToUtf16(oldPath), Utf8::ToUtf16(newPath));
		return (status != 0);
#else
		auto nullTerminatedOldPath = String::nullTerminatedView(oldPath);
		auto nullTerminatedNewPath = String::nullTerminatedView(newPath);
#ifdef DEATH_TARGET_ANDROID
		if (AssetFile::assetPath(nullTerminatedOldPath.data()))
			return false;
#endif

		const int status = ::rename(nullTerminatedOldPath.data(), nullTerminatedNewPath.data());
		return (status == 0);
#endif
	}

	bool FileSystem::copy(const StringView& oldPath, const StringView& newPath)
	{
		if (oldPath == nullptr || newPath == nullptr)
			return false;

#ifdef DEATH_TARGET_WINDOWS
		const int status = ::CopyFile(Utf8::ToUtf16(oldPath), Utf8::ToUtf16(newPath), TRUE);
		return (status != 0);
#elif defined(__linux__)
		auto nullTerminatedOldPath = String::nullTerminatedView(oldPath);
		auto nullTerminatedNewPath = String::nullTerminatedView(newPath);
#ifdef DEATH_TARGET_ANDROID
		if (AssetFile::assetPath(nullTerminatedOldPath.data()))
			return false;
#endif

		int source, dest;
		off_t bytes = 0;
		struct stat sb;

		if ((source = open(nullTerminatedOldPath.data(), O_RDONLY)) == -1)
			return false;

		fstat(source, &sb);
		if ((dest = creat(nullTerminatedNewPath.data(), sb.st_mode)) == -1) {
			close(source);
			return false;
		}

		const int status = sendfile(dest, source, &bytes, sb.st_size);

		close(source);
		close(dest);

		return (status != -1);
#else
		const size_t BufferSize = 4096;
		char buffer[BufferSize];

		int source, dest;
		size_t size = 0;
		struct stat sb;

		if ((source = open(String::nullTerminatedView(oldPath).data(), O_RDONLY)) == -1)
			return false;

		fstat(source, &sb);
		if ((dest = open(String::nullTerminatedView(newPath).data(), O_WRONLY | O_CREAT, sb.st_mode)) == -1) {
			close(source);
			return false;
		}

		while ((size = read(source, buffer, BufferSize)) > 0)
			write(dest, buffer, size);

		close(source);
		close(dest);

		return true;
#endif
	}

	long int FileSystem::fileSize(const StringView& path)
	{
		if (path == nullptr)
			return -1;

#ifdef DEATH_TARGET_WINDOWS
		HANDLE hFile = ::CreateFile(Utf8::ToUtf16(path), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
		LARGE_INTEGER fileSize;
		fileSize.QuadPart = 0;
		const int status = GetFileSizeEx(hFile, &fileSize);
		const int closed = CloseHandle(hFile);
		return (status != 0 && closed != 0) ? static_cast<long int>(fileSize.QuadPart) : -1;
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#ifdef DEATH_TARGET_ANDROID
		if (AssetFile::assetPath(nullTerminatedPath.data()))
			return AssetFile::length(nullTerminatedPath.data());
#endif

		struct stat sb;
		const bool statCalled = callStat(nullTerminatedPath.data(), sb);
		if (statCalled == false)
			return -1;

		return sb.st_size;
#endif
	}

	FileSystem::FileDate FileSystem::lastModificationTime(const StringView& path)
	{
		FileDate date = { };
#ifdef DEATH_TARGET_WINDOWS
		HANDLE hFile = ::CreateFile(Utf8::ToUtf16(path), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
		FILETIME fileTime;
		if (GetFileTime(hFile, nullptr, nullptr, &fileTime)) {
			SYSTEMTIME sysTime;
			FileTimeToSystemTime(&fileTime, &sysTime);
			date = nativeTimeToFileDate(&sysTime, *(int64_t*)&fileTime);
		}
		const int closed = CloseHandle(hFile);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#ifdef DEATH_TARGET_ANDROID
		if (AssetFile::assetPath(nullTerminatedPath.data()))
			return date;
#endif

		struct stat sb;
		const bool statCalled = callStat(nullTerminatedPath.data(), sb);
		if (statCalled)
			date = nativeTimeToFileDate(&sb.st_mtime);
#endif

		return date;
	}

	FileSystem::FileDate FileSystem::lastAccessTime(const StringView& path)
	{
		FileDate date = { };
#ifdef DEATH_TARGET_WINDOWS
		HANDLE hFile = ::CreateFile(Utf8::ToUtf16(path), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
		FILETIME fileTime;
		if (GetFileTime(hFile, nullptr, &fileTime, nullptr)) {
			SYSTEMTIME sysTime;
			FileTimeToSystemTime(&fileTime, &sysTime);
			date = nativeTimeToFileDate(&sysTime, *(int64_t*)&fileTime);
		}
		const int closed = CloseHandle(hFile);
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#ifdef DEATH_TARGET_ANDROID
		if (AssetFile::assetPath(nullTerminatedPath.data()))
			return date;
#endif

		struct stat sb;
		const bool statCalled = callStat(nullTerminatedPath.data(), sb);
		if (statCalled)
			date = nativeTimeToFileDate(&sb.st_atime);
#endif

		return date;
	}

	int FileSystem::permissions(const StringView& path)
	{
		if (path == nullptr)
			return 0;

#ifdef DEATH_TARGET_WINDOWS
		int mode = Permission::READ;
		mode += isExecutable(path) ? Permission::EXECUTE : 0;
		const DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY) == 0)
			mode += Permission::WRITE;
		return mode;
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#ifdef DEATH_TARGET_ANDROID
		if (AssetFile::assetPath(nullTerminatedPath.data())) {
			if (AssetFile::tryOpenDirectory(nullTerminatedPath.data()))
				return (Permission::READ + Permission::EXECUTE);
			else if (AssetFile::tryOpenFile(nullTerminatedPath.data()))
				return Permission::READ;
		}
#endif

		struct stat sb;
		const bool statCalled = callStat(nullTerminatedPath.data(), sb);
		if (statCalled == false)
			return 0;

		return nativeModeToEnum(sb.st_mode);
#endif
	}

	bool FileSystem::changePermissions(const StringView& path, int mode)
	{
		if (path == nullptr)
			return false;

#ifdef DEATH_TARGET_WINDOWS
		DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		if (attrs != INVALID_FILE_ATTRIBUTES) {
			// Adding the write permission
			if ((mode & Permission::WRITE) && (attrs & FILE_ATTRIBUTE_READONLY)) {
				attrs &= ~FILE_ATTRIBUTE_READONLY;
				const int status = ::SetFileAttributes(Utf8::ToUtf16(path), attrs);
				return (status != 0);
			}
			// Removing the write permission
			else if ((mode & Permission::WRITE) == 0 && (attrs & FILE_ATTRIBUTE_READONLY) == 0) {
				attrs |= FILE_ATTRIBUTE_READONLY;
				const int status = ::SetFileAttributes(Utf8::ToUtf16(path), attrs);
				return (status != 0);
			}
			return true;
		}
		return false;
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#ifdef DEATH_TARGET_ANDROID
		if (AssetFile::assetPath(nullTerminatedPath.data()))
			return false;
#endif

		struct stat sb;
		const bool statCalled = callStat(nullTerminatedPath.data(), sb);
		if (statCalled == false)
			return false;

		const unsigned int currentMode = sb.st_mode;
		unsigned int newMode = addPermissionsToCurrent(currentMode, mode);
		const int complementMode = EXECUTE + WRITE + READ - mode;
		newMode = removePermissionsFromCurrent(newMode, complementMode);

		const int status = chmod(nullTerminatedPath.data(), newMode);
		return (status == 0);
#endif
	}

	bool FileSystem::addPermissions(const StringView& path, int mode)
	{
		if (path == nullptr)
			return false;

#ifdef DEATH_TARGET_WINDOWS
		DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		if (attrs != INVALID_FILE_ATTRIBUTES) {
			// Adding the write permission
			if ((mode & Permission::WRITE) && (attrs & FILE_ATTRIBUTE_READONLY)) {
				attrs &= ~FILE_ATTRIBUTE_READONLY;
				const int status = ::SetFileAttributes(Utf8::ToUtf16(path), attrs);
				return (status != 0);
			}
			return true;
		}
		return false;
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#ifdef DEATH_TARGET_ANDROID
		if (AssetFile::assetPath(nullTerminatedPath.data()))
			return false;
#endif

		struct stat sb;
		const bool statCalled = callStat(nullTerminatedPath.data(), sb);
		if (statCalled == false)
			return false;

		const unsigned int currentMode = sb.st_mode;
		const unsigned int newMode = addPermissionsToCurrent(currentMode, mode);
		const int status = chmod(nullTerminatedPath.data(), newMode);
		return (status == 0);
#endif
	}

	bool FileSystem::removePermissions(const StringView& path, int mode)
	{
		if (path == nullptr)
			return false;

#ifdef DEATH_TARGET_WINDOWS
		DWORD attrs = ::GetFileAttributes(Utf8::ToUtf16(path));
		if (attrs != INVALID_FILE_ATTRIBUTES) {
			// Removing the write permission
			if ((mode & Permission::WRITE) && (attrs & FILE_ATTRIBUTE_READONLY) == 0) {
				attrs |= FILE_ATTRIBUTE_READONLY;
				const int status = ::SetFileAttributes(Utf8::ToUtf16(path), attrs);
				return (status != 0);
			}
			return true;
		}
		return false;
#else
		auto nullTerminatedPath = String::nullTerminatedView(path);
#ifdef DEATH_TARGET_ANDROID
		if (AssetFile::assetPath(nullTerminatedPath.data()))
			return false;
#endif

		struct stat sb;
		const bool statCalled = callStat(nullTerminatedPath.data(), sb);
		if (statCalled == false)
			return false;

		const unsigned int currentMode = sb.st_mode;
		const unsigned int newMode = removePermissionsFromCurrent(currentMode, mode);
		const int status = chmod(nullTerminatedPath.data(), newMode);
		return (status == 0);
#endif
	}

	const String& FileSystem::savePath()
	{
		if (savePath_.empty())
			initSavePath();

		return savePath_;
	}

	///////////////////////////////////////////////////////////
	// PRIVATE FUNCTIONS
	///////////////////////////////////////////////////////////

	void FileSystem::initSavePath()
	{
#if defined(DEATH_TARGET_ANDROID)
		AndroidApplication& application = static_cast<AndroidApplication&>(theApplication());

		// Get the internal data path from the Android application
		savePath_ = application.internalDataPath();

		if (fs::isDirectory(savePath_.data()) == false) {
			// Trying to create the data directory
			if (createDir(savePath_.data()) == false) {
				LOGE_X("Cannot create directory: %s", savePath_.data());
				savePath_.clear();
			}
		}
#elif defined(DEATH_TARGET_WINDOWS)
		// TODO
		//assert(false);
		/*const char *userProfileEnv = _dupenv_s("USERPROFILE");
		if (userProfileEnv == nullptr || strlen(userProfileEnv) == 0)
		{
			const char *homeDriveEnv = getenv("HOMEDRIVE");
			const char *homePathEnv = getenv("HOMEPATH");

			if ((homeDriveEnv == nullptr || strlen(homeDriveEnv) == 0) &&
				(homePathEnv == nullptr || strlen(homePathEnv) == 0))
			{
				const char *homeEnv = getenv("HOME");
				if (homeEnv && strnlen(homeEnv, MaxPathLength))
					savePath_ = homeEnv;
			}
			else
			{
				savePath_ = homeDriveEnv;
				savePath_ += homePathEnv;
			}
		}
		else
			savePath_ = userProfileEnv;

		if (savePath_.empty() == false)
			savePath_ += "\\";*/
#else
		const char* homeEnv = getenv("HOME");

		if (homeEnv == nullptr || strnlen(homeEnv, MaxPathLength) == 0)
			savePath_ = getpwuid(getuid())->pw_dir;
		else
			savePath_ = homeEnv;

		savePath_ += "/.config/";
#endif
	}

}
