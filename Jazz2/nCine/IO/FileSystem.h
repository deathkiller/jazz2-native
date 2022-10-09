#pragma once

#include "IFileStream.h"
#include "../../Common.h"

#include <memory>

#include <Containers/StringView.h>

using namespace Death::Containers;
using namespace Death::Containers::Literals;

#ifdef DEATH_TARGET_WINDOWS
typedef void* HANDLE;
#else
#	include <climits> // for `PATH_MAX`
#	if defined(DEATH_TARGET_APPLE)
#		include <dirent.h>
#	elif defined(DEATH_TARGET_ANDROID)
using DIR = struct DIR;
using AAssetDir = struct AAssetDir;
#	else
struct __dirstream;
using DIR = struct __dirstream;
#	endif
#endif

namespace nCine
{
	/// File system related methods
	class FileSystem
	{
	public:
		/// Maximum allowed length for a path string and native path separator
#if defined(DEATH_TARGET_WINDOWS)
		static constexpr unsigned int MaxPathLength = MAX_PATH;
		static constexpr char PathSeparator[] = "\\";
#else
		static constexpr unsigned int MaxPathLength = PATH_MAX;
		static constexpr char PathSeparator[] = "/";
#endif

		/// The available permissions to check or set
		enum class Permission
		{
			None = 0x00,
			Read = 0x01,
			Write = 0x02,
			Execute = 0x04
		};

		DEFINE_PRIVATE_ENUM_OPERATORS(Permission);

		struct FileDate
		{
			int32_t Year;
			int32_t Month;
			int32_t Day;
			int32_t Hour;
			int32_t Minute;
			int32_t Second;

			int64_t Ticks;
		};

		enum class EnumerationOptions
		{
			None = 0x00,

			// Skip regular files
			SkipFiles = 0x01,
			// Skip directories
			SkipDirectories = 0x02,
			// Skip everything that is not a file or directory
			SkipSpecial = 0x04
		};

		DEFINE_PRIVATE_ENUM_OPERATORS(EnumerationOptions);

		/// The class that handles directory traversal
		class Directory
		{
		public:
			Directory(const StringView& path, EnumerationOptions options = EnumerationOptions::None);
			~Directory();

			/// Opens a directory for traversal
			bool Open(const StringView& path, EnumerationOptions options = EnumerationOptions::None);
			/// Closes an opened directory
			void Close();
			/// Returns the name of the next file inside the directory or `nullptr`
			const char* GetNext();

		private:
			EnumerationOptions _options;

			char _path[MaxPathLength];
			char* _fileNamePart = nullptr;
#if defined(DEATH_TARGET_WINDOWS)
			bool _firstFile = true;
			HANDLE _hFindFile = NULL;
#else
#	if defined(DEATH_TARGET_ANDROID)
			AAssetDir* _assetDir = nullptr;
#	endif
			DIR* _dirStream = nullptr;
#endif
		};

#if defined(DEATH_TARGET_WINDOWS)
		// Windows is already case in-sensitive
		static const StringView& FindPathCaseInsensitive(const StringView& path)
		{
			return path;
		}
#else
		static String FindPathCaseInsensitive(const StringView& path);
#endif

		/// Joins together two path components
		static String JoinPath(const StringView& first, const StringView& second);
		static String JoinPath(const ArrayView<const StringView> paths);
		static String JoinPath(const std::initializer_list<StringView> paths);

		/// Returns the aboslute path after joining together two path components
		static String JoinPathAbsolute(const StringView& first, const StringView& second);

		/// Returns the path up to, but not including, the final separator
		static String GetDirectoryName(const StringView& path);
		/// Returns the path component after the final separator
		static String GetFileName(const StringView& path);
		/// Returns the path component after the final separator without extension
		static String GetFileNameWithoutExtension(const StringView& path);
		/// Returns an absolute path from a relative one
		/** Also resolves dot references to current and parent directory and double separators */
		static String GetAbsolutePath(const StringView& path);

		/// Returns the extension position in the string or `nullptr` if it is not found
		static StringView GetExtension(const StringView& path);
		/// Returns true if the file at `path` as the specified extension (case-insensitive comparison)
		static bool HasExtension(const StringView& path, const StringView& extension);

		/// Returns the path of executable
		static String GetExecutablePath();
		/// Returns the path of current working directory
		static String GetWorkingDirectory();
		/// Sets the current working directory, the starting point for interpreting relative paths
		static bool SetWorkingDirectory(const StringView& path);
		/// Returns the path of the user home directory
		static String GetHomeDirectory();
#if defined(DEATH_TARGET_ANDROID)
		/// Returns the path of the Android external storage directory
		static String GetExternalStorage();
#elif defined(DEATH_TARGET_UNIX)
		static String GetLocalStorage();
#endif

		/// Returns true if the specified path is a directory
		static bool IsDirectory(const StringView& path);
		/// Returns true if the specified path is a file
		static bool IsFile(const StringView& path);

		/// Returns true if the file or directory exists
		static bool Exists(const StringView& path);
		/// Returns true if the file or directory is readable
		static bool IsReadable(const StringView& path);
		/// Returns true if the file or directory is writeable
		static bool IsWritable(const StringView& path);
		/// Returns true if the file or directory is executable
		static bool IsExecutable(const StringView& path);

		/// Returns true if the path is a file and is readable
		static bool IsReadableFile(const StringView& path);
		/// Returns true if the path is a file and is writeable
		static bool IsWritableFile(const StringView& path);

		/// Returns true if the file or directory is hidden
		static bool IsHidden(const StringView& path);
		/// Makes a file or directory hidden or not
		static bool SetHidden(const StringView& path, bool hidden);

		/// Creates a new directory
		static bool CreateDirectories(const StringView& path);
		/// Deletes an directory and all its content
		static bool RemoveDirectoryRecursive(const StringView& path);
		/// Deletes a file
		static bool RemoveFile(const StringView& path);
		/// Renames or moves a file or a directory
		static bool Rename(const StringView& oldPath, const StringView& newPath);
		/// Copies a file
		static bool Copy(const StringView& oldPath, const StringView& newPath, bool overwrite = true);

		/// Returns the file size in bytes
		static int64_t FileSize(const StringView& path);
		/// Returns the last time the file or directory was modified
		static FileDate LastModificationTime(const StringView& path);
		/// Returns the last time the file or directory was accessed
		static FileDate LastAccessTime(const StringView& path);

		/// Returns the file or directory permissions in a mask
		static Permission GetPermissions(const StringView& path);
		/// Sets the file or directory permissions to those of the mask
		static bool ChangePermissions(const StringView& path, Permission mode);
		/// Adds the permissions in the mask to a file or a directory
		static bool AddPermissions(const StringView& path, Permission mode);
		/// Removes the permissions in the mask from a file or a directory
		static bool RemovePermissions(const StringView& path, Permission mode);

#if defined(DEATH_TARGET_EMSCRIPTEN)
		/// Mounts specified path to persistent file system (Emscripten only)
		static void MountAsPersistent(const StringView& path);

		/// Saves all changes to all persistent file systems (Emscripten only)
		static void SyncToPersistent();
#endif

		/// Opens file stream with specified access mode
		static std::unique_ptr<IFileStream> Open(const String& path, FileAccessMode mode, bool shouldExitOnFailToOpen = false);

		static std::unique_ptr<IFileStream> CreateFromMemory(unsigned char* bufferPtr, uint32_t bufferSize);
		static std::unique_ptr<IFileStream> CreateFromMemory(const unsigned char* bufferPtr, uint32_t bufferSize);

		/// Returns the base directory for data loading
		inline static const String& GetDataPath() {
			return _dataPath;
		}
		/// Returns the writable directory for saving data
		static const String& GetSavePath(const StringView& applicationName);

	private:
		/// Deleted copy constructor
		FileSystem(const FileSystem&) = delete;
		/// Deleted assignment operator
		FileSystem& operator=(const FileSystem&) = delete;
		
		/// The path for the application to load files from
		static String _dataPath;
		/// The path for the application to write files into
		static String _savePath;

		/// Determines the correct save path based on the platform
		static void InitializeSavePath(const StringView& applicationName);

		/// The `AppConfiguration` class needs to access `dataPath_`
		friend class AppConfiguration;
	};

	using fs = FileSystem;

}
