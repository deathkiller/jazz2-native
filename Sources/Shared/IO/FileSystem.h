#pragma once

/** @file
	@brief Class @ref Death::IO::FileSystem
*/

#include "Stream.h"
#include "FileAccess.h"
#include "../Containers/String.h"

#include <memory>
#include <optional>

/**
	@brief Whether the target's file system matches names case-insensitively

	Decides whether @relativeref{Death::IO,FileSystem::FindPathCaseInsensitive()} has anything to do. Where
	this is defined the answer is already "the path as given", so the function is a pass-through and costs
	nothing; where it is not, the path has to be reconstructed one component at a time by enumerating each
	directory and comparing without case.

	The consoles belong here for the same reason Windows® does - not by assumption, but because the file
	systems they actually read content from match without case:
	- The PlayStation®2 mounts its disc through `cdfs`, whose names are plain uppercase ISO 9660 (the disc
	  image carries neither Rock Ridge nor Joliet), and lower-case paths open on it perfectly well.
	- The Dreamcast's `fs_iso9660` compares with @cpp tolower() @ce throughout and even lower-cases the
	  names it reports back from a directory read.
	- The PSP, the Wii and the GameCube read their content from FAT volumes, which are case-insensitive by
	  definition, a UMD or a disc is ISO 9660 like the above.

	Doing the enumeration anyway would not just be wasted work on those, it would be *expensive* work: the
	walk only runs when the path was not found as given, which on a case-insensitive file system means it
	is not there under any spelling -- and on optical media a lookup that misses costs a seek and a retry.

	The PlayStation®3 is deliberately absent: nothing here establishes how its file system compares names,
	and the pass-through is only safe where that is known.
*/
#if defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_SWITCH) || defined(DEATH_TARGET_VITA) || \
		defined(DEATH_TARGET_PS2) || defined(DEATH_TARGET_PSP) || defined(DEATH_TARGET_DREAMCAST) || \
		defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE) || defined(DOXYGEN_GENERATING_OUTPUT)
#	define DEATH_CASE_INSENSITIVE_FILESYSTEM
#endif

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief File system related methods
	*/
	class FileSystem
	{
	public:
		/** @{ @name Constants */

		// Windows 10 supports long paths everywhere and Unix systems usually also support at least 2048 characters
		/** @brief Maximum path length supported */
		static constexpr std::size_t MaxPathLength = 2048;

#if defined(DEATH_TARGET_WINDOWS)
		/** @brief Native path separator */
		static constexpr char PathSeparator[] = "\\";
#else
		/** @brief Native path separator */
		static constexpr char PathSeparator[] = "/";
#endif

		/** @} */

		/** @brief Available permissions to check or set, supports a bitwise combination of its member values */
		enum class Permission
		{
			None = 0,			/**< None */

			Read = 0x01,		/**< Read */
			Write = 0x02,		/**< Write */
			Execute = 0x04		/**< Execute */
		};

		DEATH_PRIVATE_ENUM_FLAGS(Permission);

		/** @brief Options that modify behavior of @ref Directory, supports a bitwise combination of its member values */
		enum class EnumerationOptions
		{
			/** @brief Default behavior */
			None = 0,

			/** @brief Skip regular files */
			SkipFiles = 0x01,
			/** @brief Skip directories */
			SkipDirectories = 0x02,
			/** @brief Skip everything that is not a file or directory */
			SkipSpecial = 0x04
		};

		DEATH_PRIVATE_ENUM_FLAGS(EnumerationOptions);

		/** @brief Handles directory traversal, should be used as iterator */
		class Directory
		{
		public:
#ifndef DOXYGEN_GENERATING_OUTPUT
			class Proxy;

			// Iterator defines
			using iterator_category = std::input_iterator_tag;
			using difference_type = std::ptrdiff_t;
			//using reference = const Containers::StringView&;
			using value_type = Containers::StringView;
#endif

			Directory() noexcept;
			Directory(Containers::StringView path, EnumerationOptions options = EnumerationOptions::None);
			~Directory();

			Directory(const Directory& other);
			Directory& operator=(const Directory& other);
			Directory(Directory&& other) noexcept;
			Directory& operator=(Directory&& other) noexcept;

			Containers::StringView operator*() const & noexcept;
			Directory& operator++();
			Proxy operator++(int);

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

#if defined(DEATH_CASE_INSENSITIVE_FILESYSTEM)
		/**
		 * @brief Returns path with correct case on case-sensitive platforms (or `{}` if path not found)
		 *
		 * The target's file system already matches without case (see @ref DEATH_CASE_INSENSITIVE_FILESYSTEM),
		 * so the path is returned unchanged and nothing is looked up. Note the difference in the "not found"
		 * half of the contract: this form cannot report a missing path and never returns @cpp {} @ce,
		 * so a caller that uses the result as an existence test only works on the platforms that take
		 * the branch below. The callers that do so are all inside code those platforms do not build.
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
		 * @brief Returns path with correct case on case-sensitive platforms (or `{}` if path not found)
		 *
		 * Each component of the path is looked up in turn by enumerating its parent directory and comparing
		 * without case, so a path that exists under a different spelling is returned with the spelling the
		 * file system actually uses.
		 */
		static Containers::String FindPathCaseInsensitive(Containers::StringView path);

		/** @overload */
		DEATH_ALWAYS_INLINE static Containers::String FindPathCaseInsensitive(Containers::String&& path) {
			return FindPathCaseInsensitive(Containers::StringView{path});
		}
#endif

		/** @brief Combines together specified path components */
		static Containers::String CombinePath(Containers::StringView first, Containers::StringView second);
		/** @overload */
		static Containers::String CombinePath(Containers::ArrayView<const Containers::StringView> paths);
		/** @overload */
		static Containers::String CombinePath(std::initializer_list<Containers::StringView> paths);

		/** @brief Returns the path up to, but not including, the final separator */
		static Containers::StringView GetDirectoryName(Containers::StringView path);
		/** @brief Returns the path component after the final separator */
		static Containers::StringView GetFileName(Containers::StringView path);
		/** @brief Returns the path component after the final separator without extension */
		static Containers::StringView GetFileNameWithoutExtension(Containers::StringView path);
		/** @brief Returns the extension as lower-case string without dot or empty string if it is not found */
		static Containers::String GetExtension(Containers::StringView path);
		/** @brief Converts path using native separators to forward slashes */
#if defined(DEATH_TARGET_WINDOWS)
		static Containers::String FromNativeSeparators(Containers::String path);
#else
		DEATH_ALWAYS_INLINE static Containers::StringView FromNativeSeparators(Containers::StringView path) {
			return path;
		}
#endif
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
		/** @brief Returns `true` if the specified path is not empty and is absolute */
		static bool IsAbsolutePath(Containers::StringView path);

		/** @brief Returns the path to the executable file for the running application */
		static Containers::String GetExecutablePath();
		/**
		 * @brief Returns the path to the application-specific writable directory for configuration files
		 *
		 * Unlike @ref GetSavePath(), which targets game-state storage (e.g., @cpp "Saved Games" @ce on Windows), this
		 * targets the conventional location for application settings. On Windows, the directory is usually equivalent to
		 * @cb{.bat} %APPDATA% @ce, which points to @cpp "C:\\Users\\<user>\\AppData\\Roaming\\<name>\\" @ce. On macOS,
		 * it's usually equivalent to @cpp "~/Library/Application Support/<name>/" @ce. On other Unix systems, it usually
		 * points to @cb{.sh} "${XDG_CONFIG_HOME}/<name>/" @ce or @cpp "~/.config/<name>/" @ce. On Android, the internal
		 * data directory of the application is returned. On Windows RT, the local data folder of the package is returned.
		 */
		static Containers::String GetConfigPath(Containers::StringView applicationName);
		/**
		 * @brief Returns the path to the application-specific writable directory for saving game state
		 * 
		 * On macOS, the directory is usually equivalent to @cpp "~/Library/Application Support/<name>/" @ce. On Android,
		 * it's the internal data directory of the application. On other Unix systems, it usually points to
		 * @cb{.sh} "${XDG_CONFIG_HOME}/<name>/ @ce or @cpp "~/.config/<name>/" @ce. On Windows, it's usually
		 * @cpp "C:\\Users\\<user>\\Saved Games\\<name>\\" @ce. If the parent directory doesn't exist, @cb{.bat} %APPDATA% @ce
		 * will be used instead. On Windows RT, the local data folder of the package is returned, because
		 * the application doesn't have access to the user directories.
		 */
		static Containers::String GetSavePath(Containers::StringView applicationName);
		/** @brief Returns the path of the current working directory */
		static Containers::String GetWorkingDirectory();
		/** @brief Sets the current working directory, the starting point for interpreting relative paths */
		static bool SetWorkingDirectory(Containers::StringView path);
		/**
		 * @brief Returns the path of the user home directory
		 * 
		 * On Unix and macOS, the directory is equivalent to @cb{.sh} ${HOME} @ce environment variable. On Windows,
		 * the directory is equivalent to @cb{.bat} %USERPROFILE% @ce, which usually points to @cpp "C:\\Users\\<user>\\" @ce.
		 */
		static Containers::String GetHomeDirectory();
		/**
		 * @brief Returns the path of the directory for temporary files
		 * 
		 * On Unix and macOS, the directory is usually equivalent to @cpp "/tmp/" @ce. On Windows, the directory is
		 * equivalent to @cb{.bat} %TEMP% @ce. On Android, the directory is usually equivalent to the cache
		 * directory of the package (for example @cpp "/data/user/0/<package>/cache/" @ce).
		 */
		static Containers::String GetTempDirectory();

#if defined(DEATH_TARGET_ANDROID) || defined(DOXYGEN_GENERATING_OUTPUT)
		/**
		 * @brief Returns the path of the Android external storage directory
		 *
		 * @partialsupport Available only on @ref DEATH_TARGET_ANDROID "Android" platform.
		 */
		static Containers::String GetExternalStorage();
#endif
#if defined(DEATH_TARGET_UNIX) || defined(DOXYGEN_GENERATING_OUTPUT)
		/**
		 * @brief Returns the path pointing to `${XDG_DATA_HOME}` environment variable
		 * 
		 * If @cb{.sh} ${XDG_DATA_HOME} @ce environment variable is not set, @cpp "~/.local/share/" @ce
		 * will be used instead.
		 *
		 * @partialsupport Available only on @ref DEATH_TARGET_UNIX "Unix" platform.
		 */
		static Containers::String GetLocalStorage();
#endif
#if defined(DEATH_TARGET_WINDOWS) || defined(DOXYGEN_GENERATING_OUTPUT)
		/**
		 * @brief Returns the path of Windows® directory
		 *
		 * @partialsupport Available only on @ref DEATH_TARGET_WINDOWS "Windows" platform.
		 */
		static Containers::String GetWindowsDirectory();
#endif

		/** @brief Returns `true` if the specified path is a directory */
		static bool DirectoryExists(Containers::StringView path);
		/** @brief Returns `true` if the specified path is a file */
		static bool FileExists(Containers::StringView path);

		/** @brief Returns `true` if the file or directory exists */
		static bool Exists(Containers::StringView path);
		/** @brief Returns `true` if the file or directory is readable */
		static bool IsReadable(Containers::StringView path);
		/** @brief Returns `true` if the file or directory is writeable */
		static bool IsWritable(Containers::StringView path);
		/** @brief Returns `true` if the file or directory is executable */
		static bool IsExecutable(Containers::StringView path);

		/** @brief Returns `true` if the path is a file and is readable */
		static bool IsReadableFile(Containers::StringView path);
		/** @brief Returns `true` if the path is a file and is writeable */
		static bool IsWritableFile(Containers::StringView path);

		/** @brief Returns `true` if the path is a symbolic link */
		static bool IsSymbolicLink(Containers::StringView path);

		/** @brief Returns `true` if the file or directory is hidden */
		static bool IsHidden(Containers::StringView path);
		/** @brief Makes a file or directory hidden or not */
		static bool SetHidden(Containers::StringView path, bool hidden);
		/** @brief Returns `true` if the file or directory is read-only */
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

		/** @brief Returns permissions of a given file or directory */
		static Permission GetPermissions(Containers::StringView path);
		/** @brief Sets the file or directory permissions to those of the mask */
		static bool ChangePermissions(Containers::StringView path, Permission mode);
		/** @brief Adds permissions in the mask to a file or a directory */
		static bool AddPermissions(Containers::StringView path, Permission mode);
		/** @brief Removes permissions in the mask from a file or a directory */
		static bool RemovePermissions(Containers::StringView path, Permission mode);

		/** @brief Tries to open specified directory in operating system's file manager */
		static bool LaunchDirectoryAsync(Containers::StringView path);

#if defined(DEATH_TARGET_EMSCRIPTEN) || defined(DOXYGEN_GENERATING_OUTPUT)
		/**
		 * @brief Mounts specified path to persistent file system
		 *
		 * @partialsupport Available only on @ref DEATH_TARGET_EMSCRIPTEN "Emscripten" platform.
		 */
		static bool MountAsPersistent(Containers::StringView path);

		/**
		 * @brief Saves all changes to all persistent file systems
		 *
		 * @partialsupport Available only on @ref DEATH_TARGET_EMSCRIPTEN "Emscripten" platform.
		 */
		static void SyncToPersistent();
#endif

		/** @brief Opens a file stream with specified access mode */
		static std::unique_ptr<Stream> Open(Containers::StringView path, FileAccess mode, std::int32_t bufferSize = 8192);

#	if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX) || (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DOXYGEN_GENERATING_OUTPUT)
		/**
			@brief Memory-mapped file deleter
		
			@partialsupport Available on all platforms except @ref DEATH_TARGET_EMSCRIPTEN "Emscripten",
				@ref DEATH_TARGET_SWITCH "Nintendo Switch" and @ref DEATH_TARGET_WINDOWS_RT "Windows RT".
		*/
		class MapDeleter
		{
#	if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX)
		public:
			constexpr explicit MapDeleter() : _fd{} {}
			constexpr explicit MapDeleter(int fd) noexcept : _fd{fd} {}
			void operator()(const char* data, std::size_t size);
		private:
			int _fd;
#	elif defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		public:
			constexpr explicit MapDeleter() : _hFile{}, _hMap{} {}
			constexpr explicit MapDeleter(void* hFile, void* hMap) noexcept : _hFile{hFile}, _hMap{hMap} {}
			void operator()(const char* data, std::size_t size);
		private:
			void* _hFile;
			void* _hMap;
#	endif
		};

		/**
		 * @brief Maps a file for reading and/or writing
		 *
		 * Maps the file as a read-write memory. The array deleter takes care of unmapping. If the file doesn't exist
		 * or an error occurs while mapping, returns @ref std::nullopt_t. If the file is empty it's only opened
		 * but not mapped and a zero-sized @cpp nullptr @ce array is returned, with the deleter containing the
		 * open file handle.
		 * 
		 * @m_class{m-block m-warning}
		 * 
		 * @par Reading and writing a file while it's mapped
		 *		On @ref DEATH_TARGET_UNIX "Unix"-like systems, it's possible to write to a file that's currently
		 *		mapped with this function and the changes will be reflected to the mapping. The mapped size
		 *		however cannot change --- if the file gets longer, the additional data will not be present
		 *		in the mapping, if it gets shorter, the suffix gets filled with @cpp '\0' @ce bytes.
		 * @par
		 *		On @ref DEATH_TARGET_WINDOWS "Windows", it's not possible to open a file for writing while it's
		 *		mapped with this function. Doing so will result in an *Invalid Argument* error.
		 * @par
		 *		Opening a file for reading while it's mapped with this function works on all platforms and gives
		 *		back the same contents as the (potentially updated) mapped memory.
		 * 
		 * @partialsupport Available on all platforms except @ref DEATH_TARGET_EMSCRIPTEN "Emscripten",
		 *		@ref DEATH_TARGET_SWITCH "Nintendo Switch" and @ref DEATH_TARGET_WINDOWS_RT "Windows RT".
		 */
		static std::optional<Containers::Array<char, MapDeleter>> OpenAsMemoryMapped(Containers::StringView path, FileAccess mode);
#endif
	};

	/** @brief Convenient shortcut to @ref FileSystem */
	using fs = FileSystem;
}}