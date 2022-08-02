#pragma once

#include "../../Common.h"

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
#ifdef DEATH_TARGET_WINDOWS
		static constexpr unsigned int MaxPathLength = MAX_PATH;
		static constexpr char PathSeparator[] = "\\";
#else
		static constexpr unsigned int MaxPathLength = PATH_MAX;
		static constexpr char PathSeparator[] = "/";
#endif

		/// The available permissions to check or set
		enum Permission
		{
			READ = 1,
			WRITE = 2,
			EXECUTE = 4
		};

		struct FileDate
		{
			int year;
			int month;
			int day;
			int weekDay;
			int hour;
			int minute;
			int second;
		};

		/// The class that handles directory traversal
		class Directory
		{
		public:
			Directory(const StringView& path);
			~Directory();

			/// Opens a directory for traversal
			bool open(const StringView& path);
			/// Closes an opened directory
			void close();
			/// Returns the name of the next file inside the directory or `nullptr`
			const char* readNext();

		private:
#ifdef DEATH_TARGET_WINDOWS
			bool firstFile_ = true;
			HANDLE hFindFile_ = NULL;
			char fileName_[260];
#else
#ifdef DEATH_TARGET_ANDROID
			AAssetDir* assetDir_ = nullptr;
#endif
			DIR* dirStream_ = nullptr;
#endif
		};

		/// Joins together two path components
		static String joinPath(const StringView& first, const StringView& second);
		static String joinPath(const ArrayView<const StringView> paths);
		static String joinPath(const std::initializer_list<StringView> paths);

		/// Returns the aboslute path after joining together two path components
		static String absoluteJoinPath(const StringView& first, const StringView& second);

		/// Returns the path up to, but not including, the final separator
		static String dirName(const StringView& path);
		/// Returns the path component after the final separator
		static String baseName(const StringView& path);
		/// Returns an absolute path from a relative one
		/** Also resolves dot references to current and parent directory and double separators */
		static String absolutePath(const StringView& path);

		/// Returns the extension position in the string or `nullptr` if it is not found
		static StringView extension(const StringView& path);
		/// Returns true if the file at `path` as the specified extension (case-insensitive comparison)
		static bool hasExtension(const StringView& path, const StringView& extension);

		/// Returns the path of current directory
		static String currentDir();
		/// Sets the current working directory, the starting point for interpreting relative paths
		static bool setCurrentDir(const StringView& path);
		/// Returns the path of the user home directory
		static String homeDir();
#ifdef DEATH_TARGET_ANDROID
		/// Returns the path of the Android external storage directory
		static String externalStorageDir();
#endif

		/// Returns true if the specified path is a directory
		static bool isDirectory(const StringView& path);
		/// Returns true if the specified path is a file
		static bool isFile(const StringView& path);

		/// Returns true if the file or directory exists
		static bool exists(const StringView& path);
		/// Returns true if the file or directory is readable
		static bool isReadable(const StringView& path);
		/// Returns true if the file or directory is writeable
		static bool isWritable(const StringView& path);
		/// Returns true if the file or directory is executable
		static bool isExecutable(const StringView& path);

		/// Returns true if the path is a file and is readable
		static bool isReadableFile(const StringView& path);
		/// Returns true if the path is a file and is writeable
		static bool isWritableFile(const StringView& path);

		/// Returns true if the file or directory is hidden
		static bool isHidden(const StringView& path);
		/// Makes a file or directory hidden or not
		static bool setHidden(const StringView& path, bool hidden);

		/// Creates a new directory
		static bool createDir(const StringView& path);
		/// Deletes an empty directory
		static bool deleteEmptyDir(const StringView& path);
		/// Deletes a file
		static bool deleteFile(const StringView& path);
		/// Renames or moves a file or a directory
		static bool rename(const StringView& oldPath, const StringView& newPath);
		/// Copies a file
		static bool copy(const StringView& oldPath, const StringView& newPath);

		/// Returns the file size in bytes
		static long int fileSize(const StringView& path);
		/// Returns the last time the file or directory was modified
		static FileDate lastModificationTime(const StringView& path);
		/// Returns the last time the file or directory was accessed
		static FileDate lastAccessTime(const StringView& path);

		/// Returns the file or directory permissions in a mask
		static int permissions(const StringView& path);
		/// Sets the file or directory permissions to those of the mask
		static bool changePermissions(const StringView& path, int mode);
		/// Adds the permissions in the mask to a file or a directory
		static bool addPermissions(const StringView& path, int mode);
		/// Removes the permissions in the mask from a file or a directory
		static bool removePermissions(const StringView& path, int mode);

		/// Returns the base directory for data loading
		inline static const String& dataPath() {
			return dataPath_;
		}
		/// Returns the writable directory for saving data
		static const String& savePath();

	private:
		/// The path for the application to load files from
		static String dataPath_;
		/// The path for the application to write files into
		static String savePath_;

		/// Determines the correct save path based on the platform
		static void initSavePath();

		/// The `AppConfiguration` class needs to access `dataPath_`
		friend class AppConfiguration;
	};

	using fs = FileSystem;

}
