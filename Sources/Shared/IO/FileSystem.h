#pragma once

#include "Stream.h"
#include "FileAccess.h"
#include "../Common.h"
#include "../Containers/DateTime.h"
#include "../Containers/String.h"
#include "../Containers/StringView.h"

#include <memory>
#include <optional>

#if !defined(DEATH_TARGET_WINDOWS)
#	include <climits> // for `PATH_MAX`
#	if defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_SWITCH)
#		include <dirent.h>
#	elif defined(DEATH_TARGET_ANDROID)
using DIR = struct DIR;
using AAssetDir = struct AAssetDir;
#	elif defined(__FreeBSD__)
struct _dirdesc;
using DIR = struct _dirdesc;
#	else
struct __dirstream;
using DIR = struct __dirstream;
#	endif
#endif

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief File system related methods
	*/
	class FileSystem
	{
	public:
#if defined(DEATH_TARGET_WINDOWS)
		// Windows 10 supports long paths everywhere, so increase it a bit
		static constexpr std::uint32_t MaxPathLength = /*MAX_PATH*/2048;
		static constexpr char PathSeparator[] = "\\";
#else
		static constexpr std::uint32_t MaxPathLength = PATH_MAX;
		static constexpr char PathSeparator[] = "/";
#endif

		/** @brief The available permissions to check or set, supports a bitwise combination of its member values */
		enum class Permission
		{
			None = 0,

			Read = 0x01,
			Write = 0x02,
			Execute = 0x04
		};

		DEFINE_PRIVATE_ENUM_OPERATORS(Permission);

		/** @brief Options that modify behavior of @ref Directory, supports a bitwise combination of its member values */
		enum class EnumerationOptions
		{
			None = 0,

			/** @brief Skip regular files */
			SkipFiles = 0x01,
			/** @brief Skip directories */
			SkipDirectories = 0x02,
			/** @brief Skip everything that is not a file or directory */
			SkipSpecial = 0x04
		};

		DEFINE_PRIVATE_ENUM_OPERATORS(EnumerationOptions);

		/** @brief The class that handles directory traversal, should be used as iterator */
		class Directory
		{
		public:
			class Proxy
			{
				friend class Directory;

			public:
				Containers::StringView operator*() const & noexcept;

			private:
				explicit Proxy(Containers::StringView path);

				Containers::String _path;
			};

			// Iterator defines
			using iterator_category = std::input_iterator_tag;
			using difference_type = std::ptrdiff_t;
			//using reference = const Containers::StringView&;
			using value_type = Containers::StringView;

			Directory() noexcept;
			Directory(Containers::StringView path, EnumerationOptions options = EnumerationOptions::None);
			~Directory();

			Directory(const Directory& other);
			Directory& operator=(const Directory& other);
			Directory(Directory&& other) noexcept;
			Directory& operator=(Directory&& other) noexcept;

			Containers::StringView operator*() const & noexcept;
			Directory& operator++();

			Proxy operator++(int) {
				Proxy p{**this};
				++*this;
				return p;
			}

			bool operator==(const Directory& other) const;
			bool operator!=(const Directory& other) const;

			Directory begin() noexcept {
				return *this;
			}

			Directory end() noexcept {
				return Directory();
			}

		private:
			class Impl;
			std::shared_ptr<Impl> _impl;
		};

		FileSystem() = delete;
		~FileSystem() = delete;

#if defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_SWITCH)
		/**
		 * @brief Returns path with correct case on case-sensitive platforms (or nothing if path not found)
		 * 
		 * Windows® is already case-insensitive by default, so no validation is performed.
		 */
		DEATH_ALWAYS_INLINE static Containers::StringView FindPathCaseInsensitive(Containers::StringView path) {
			return path;
		}

		/** @overload */
		DEATH_ALWAYS_INLINE static Containers::String FindPathCaseInsensitive(Containers::String&& path) {
			return path;
		}
#else
		/**
		 * @brief Returns path with correct case on case-sensitive platforms (or nothing if path not found)
		 */
		static Containers::String FindPathCaseInsensitive(Containers::StringView path);

		/** @overload */
		DEATH_ALWAYS_INLINE static Containers::String FindPathCaseInsensitive(Containers::String&& path) {
			return FindPathCaseInsensitive(Containers::StringView{path});
		}
#endif

		/** @brief Combines together specified path components */
		static Containers::String CombinePath(Containers::StringView first, Containers::StringView second);
		static Containers::String CombinePath(Containers::ArrayView<const Containers::StringView> paths);
		static Containers::String CombinePath(std::initializer_list<Containers::StringView> paths);

		/** @brief Returns the path up to, but not including, the final separator */
		static Containers::StringView GetDirectoryName(Containers::StringView path);
		/** @brief Returns the path component after the final separator */
		static Containers::StringView GetFileName(Containers::StringView path);
		/** @brief Returns the path component after the final separator without extension */
		static Containers::StringView GetFileNameWithoutExtension(Containers::StringView path);
		/** @brief Returns the extension as lower-case string without dot or empty string if it is not found */
		static Containers::String GetExtension(Containers::StringView path);
		/** @brief Converts path using forward slashes to native separators */
#if defined(DEATH_TARGET_WINDOWS)
		static Containers::String ToNativeSeparators(Containers::String path);
#else
		DEATH_ALWAYS_INLINE static Containers::StringView ToNativeSeparators(Containers::StringView path) {
			return path;
		}
#endif

		/** @brief Returns an absolute path from a relative one */
		static Containers::String GetAbsolutePath(Containers::StringView path);
		/** @brief Returns true if the specified path is not empty and is absolute */
		static bool IsAbsolutePath(Containers::StringView path);

		/** @brief Returns the path of executable */
		static Containers::String GetExecutablePath();
		/** @brief Returns the path of current working directory */
		static Containers::String GetWorkingDirectory();
		/** @brief Sets the current working directory, the starting point for interpreting relative paths */
		static bool SetWorkingDirectory(Containers::StringView path);
		/** @brief Returns the path of the user home directory */
		static Containers::String GetHomeDirectory();
#if defined(DEATH_TARGET_ANDROID)
		/** @brief Returns the path of the Android external storage directory */
		static Containers::String GetExternalStorage();
#elif defined(DEATH_TARGET_UNIX)
		/** @brief Returns the path pointing to `XDG_DATA_HOME` or `~/.local/share/` in the most cases */
		static Containers::String GetLocalStorage();
#elif defined(DEATH_TARGET_WINDOWS)
		/** @brief Returns the path of Windows® directory */
		static Containers::String GetWindowsDirectory();
#endif

		/** @brief Returns true if the specified path is a directory */
		static bool DirectoryExists(Containers::StringView path);
		/** @brief Returns true if the specified path is a file */
		static bool FileExists(Containers::StringView path);

		/** @brief Returns true if the file or directory exists */
		static bool Exists(Containers::StringView path);
		/** @brief Returns true if the file or directory is readable */
		static bool IsReadable(Containers::StringView path);
		/** @brief Returns true if the file or directory is writeable */
		static bool IsWritable(Containers::StringView path);
		/** @brief Returns true if the file or directory is executable */
		static bool IsExecutable(Containers::StringView path);

		/** @brief Returns true if the path is a file and is readable */
		static bool IsReadableFile(Containers::StringView path);
		/** @brief Returns true if the path is a file and is writeable */
		static bool IsWritableFile(Containers::StringView path);

		/** @brief Returns true if the path is a symbolic link */
		static bool IsSymbolicLink(Containers::StringView path);

		/** @brief Returns true if the file or directory is hidden */
		static bool IsHidden(Containers::StringView path);
		/** @brief Makes a file or directory hidden or not */
		static bool SetHidden(Containers::StringView path, bool hidden);
		/** @brief Returns true if the file or directory is read-only */
		static bool IsReadOnly(Containers::StringView path);
		/** @brief Makes a file or directory read-only or not */
		static bool SetReadOnly(Containers::StringView path, bool readonly);

		/** @brief Creates a new directory */
		static bool CreateDirectories(Containers::StringView path);
		/** @brief Deletes an directory and all its content */
		static bool RemoveDirectoryRecursive(Containers::StringView path);
		/** @brief Deletes a file */
		static bool RemoveFile(Containers::StringView path);
		/** @brief Renames or moves a file or a directory */
		static bool Move(Containers::StringView oldPath, Containers::StringView newPath);
		/** @brief Moves a file or a directory to trash */
		static bool MoveToTrash(Containers::StringView path);
		/** @brief Copies a file */
		static bool Copy(Containers::StringView oldPath, Containers::StringView newPath, bool overwrite = true);

		/** @brief Returns the file size in bytes */
		static std::int64_t GetFileSize(Containers::StringView path);
		/** @brief Returns the creation time of the file or directory (if available) */
		static Containers::DateTime GetCreationTime(Containers::StringView path);
		/** @brief Returns the last time the file or directory was modified */
		static Containers::DateTime GetLastModificationTime(Containers::StringView path);
		/** @brief Returns the last time the file or directory was accessed */
		static Containers::DateTime GetLastAccessTime(Containers::StringView path);

		/** @brief Returns the file or directory permissions in a mask */
		static Permission GetPermissions(Containers::StringView path);
		/** @brief Sets the file or directory permissions to those of the mask */
		static bool ChangePermissions(Containers::StringView path, Permission mode);
		/** @brief Adds the permissions in the mask to a file or a directory */
		static bool AddPermissions(Containers::StringView path, Permission mode);
		/** @brief Removes the permissions in the mask from a file or a directory */
		static bool RemovePermissions(Containers::StringView path, Permission mode);

		/** @brief Tries to open specified directory in operating system's file manager */
		static bool LaunchDirectoryAsync(Containers::StringView path);

#if defined(DEATH_TARGET_EMSCRIPTEN) || defined(DOXYGEN_GENERATING_OUTPUT)
		/**
		 * @brief Mounts specified path to persistent file system (Emscripten only)
		 *
		 * @partialsupport Available only on @ref DEATH_TARGET_EMSCRIPTEN "Emscripten" platform.
		 */
		static void MountAsPersistent(Containers::StringView path);

		/**
		 * @brief Saves all changes to all persistent file systems (Emscripten only)
		 *
		 * @partialsupport Available only on @ref DEATH_TARGET_EMSCRIPTEN "Emscripten" platform.
		 */
		static void SyncToPersistent();
#endif

		/** @brief Opens file stream with specified access mode */
		static std::unique_ptr<Stream> Open(Containers::StringView path, FileAccess mode);

#if defined(DEATH_TARGET_UNIX) || (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DOXYGEN_GENERATING_OUTPUT)
		/**
			@brief Memory-mapped file deleter
		
			@partialsupport Available only on @ref DEATH_TARGET_UNIX "Unix" and non-RT @ref DEATH_TARGET_WINDOWS "Windows" platforms.
		*/
		class MapDeleter
		{
#	if defined(DEATH_TARGET_UNIX)
		public:
			constexpr explicit MapDeleter() : _fd {} {}
			constexpr explicit MapDeleter(int fd) noexcept : _fd { fd } {}
			void operator()(const char* data, std::size_t size);
		private:
			int _fd;
#	elif defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		public:
			constexpr explicit MapDeleter() : _hFile {}, _hMap {} {}
			constexpr explicit MapDeleter(void* hFile, void* hMap) noexcept : _hFile { hFile }, _hMap { hMap } {}
			void operator()(const char* data, std::size_t size);
		private:
			void* _hFile;
			void* _hMap;
#	endif
		};

		/**
		 * @brief Maps a file for reading and writing
		 *
		 * Maps the file as a read-write memory. The array deleter takes care of unmapping. If the file doesn't exist
		 * or an error occurs while mapping, returns @ref std::nullopt_t. If the file is empty it's only opened
		 * but not mapped and a zero-sized @cpp nullptr @ce array is returned, with the deleter containing the
		 * open file handle.
		 * 
		 * @partialsupport Available only on @ref DEATH_TARGET_UNIX "Unix" and non-RT @ref DEATH_TARGET_WINDOWS "Windows" platforms.
		 */
		static std::optional<Containers::Array<char, MapDeleter>> OpenAsMemoryMapped(Containers::StringView path, FileAccess mode);
#endif

		/** @brief Creates a stream from a specified region of memory */
		static std::unique_ptr<Stream> CreateFromMemory(std::uint8_t* bufferPtr, std::int32_t bufferSize);
		/** @overload */
		static std::unique_ptr<Stream> CreateFromMemory(const std::uint8_t* bufferPtr, std::int32_t bufferSize);

		/** @brief Returns application-specific writable directory for saving data */
		static const Containers::String& GetSavePath(Containers::StringView applicationName);

	private:
		static Containers::String _savePath;

		static void InitializeSavePath(Containers::StringView applicationName);
	};

	/** @brief Convenient shortcut to @ref FileSystem */
	using fs = FileSystem;
}}