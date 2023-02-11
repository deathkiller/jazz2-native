#pragma once

#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	/// The class that manages the cache of binary OpenGL shader programs
	class BinaryShaderCache
	{
	public:
		/// The statistics about the cache and its requests
		struct Statistics
		{
			unsigned int LoadedShaders = 0;
			unsigned int SavedShaders = 0;
			unsigned int PlatformFilesCount = 0;
			unsigned int PlatformBytesCount = 0;
			unsigned int TotalFilesCount = 0;
			unsigned int TotalBytesCount = 0;
		};

		BinaryShaderCache(const StringView& path);

		inline bool isAvailable() const {
			return isAvailable_;
		}

		inline uint64_t platformHash() const {
			return platformHash_;
		}
		uint64_t hashSources(unsigned int count, const char** strings, int* lengths) const;

		/// Returns the size in bytes of the binary shader from the cache with the given format and hash id
		unsigned int binarySize(uint32_t format, uint64_t hash);
		/// Loads a binary shader from the cache with the given format and hash id
		const void* loadFromCache(uint32_t format, uint64_t hash);
		/// Saves a binary shader to the cache with the given format and hash id
		bool saveToCache(int length, const void* buffer, uint32_t format, uint64_t hash);

		/// Deletes all binary shaders that not belong to this platform from the cache directory
		void prune();
		/// Deletes all binary shaders from the cache directory
		void clear();

		/// Returns the statistics about the files in the cache
		inline const Statistics& statistics() const {
			return statistics_;
		}

		/// Returns the current cache directory for binary shaders
		inline const StringView path() {
			return path_;
		}
		/// Sets a new directory as the cache for binary shaders
		bool setPath(const StringView& path);

	private:
		/// A flag that indicates if the OpenGL context supports binary shaders and the cache is available
		bool isAvailable_;

		/// The hash value that identifies a specific OpenGL platform
		uint64_t platformHash_;

		/// The cache directory containing the binary shaders
		String path_;

		/// The statistics structure about the files in the cache
		Statistics statistics_;

		/// Scans the cache directory to collect statistics
		void collectStatistics();
		/// Resets all statistics to the initial values
		void clearStatistics();

		/// Returns true if the filename matches the binary shader filename format
		inline bool isShaderPath(const StringView& path) {
			return parseShaderPath(path, nullptr, nullptr, nullptr);
		}

		/// Splits a binary shader filename in its platform hash, format, and shader hash components
		bool parseShaderPath(const StringView& path, uint64_t* platformHash, uint64_t* format, uint64_t* shaderHash);

		/// Deleted copy constructor
		BinaryShaderCache(const BinaryShaderCache&) = delete; // TODO: DELETED?
		/// Deleted assignment operator
		BinaryShaderCache& operator=(const BinaryShaderCache&) = delete; // TODO: DELETED?
	};
}
