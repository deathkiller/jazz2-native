#pragma once

#include "../Common.h"
#include "../Containers/Array.h"
#include "../Containers/String.h"
#include "FileStream.h"
#include "FileSystem.h"

#include <memory>

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/** @brief Preferred file compression, used in @ref PakWriter::AddFile() */
	enum class PakPreferredCompression
	{
		None,				/**< None */
		Deflate,			/**< Deflate */
		Lz4,				/**< LZ4 */
		Zstd,				/**< Zstandard */

		Lzma2Compressed		/**< LZMA2 - Not implemented */
	};

	/**
		@brief Provides read-only access to contents of `.pak` file
	*/
	class PakFile
	{
		friend class PakWriter;

	public:
		explicit PakFile(Containers::StringView path);

		PakFile(const PakFile&) = delete;
		PakFile& operator=(const PakFile&) = delete;
		
		explicit operator bool() const {
			return IsValid();
		}

		/** @brief Returns mount point for the container */
		Containers::StringView GetMountPoint() const;
		/** @brief Returns path of the `.pak` file */
		Containers::StringView GetPath() const;
		
		bool IsValid() const;

		/** @brief Returns `true` if the specified path is a file */
		bool FileExists(Containers::StringView path);
		/** @overload */
		bool FileExists(std::uint64_t hashedPath);
		/** @brief Returns `true` if the specified path is a directory */
		bool DirectoryExists(Containers::StringView path);

		/** @brief Opens a file stream */
		std::unique_ptr<Stream> OpenFile(Containers::StringView path, std::int32_t bufferSize = FileStream::DefaultBufferSize);
		/** @overload */
		std::unique_ptr<Stream> OpenFile(std::uint64_t hashedPath, std::int32_t bufferSize = FileStream::DefaultBufferSize);

		/** @brief Handles directory traversal, should be used as iterator */
		class Directory
		{
		public:
#ifndef DOXYGEN_GENERATING_OUTPUT
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
#endif

			Directory() noexcept;
			Directory(PakFile& pakFile, Containers::StringView path, FileSystem::EnumerationOptions options = FileSystem::EnumerationOptions::None);
			~Directory();

			Directory(const Directory& other);
			Directory& operator=(const Directory& other);
			Directory(Directory&& other) noexcept;
			Directory& operator=(Directory&& other) noexcept;

			Containers::StringView operator*() const& noexcept;
			Directory& operator++();

			Proxy operator++(int) {
				Proxy p{**this};
				++*this;
				return p;
			}

			bool operator==(const Directory& rhs) const;
			bool operator!=(const Directory& rhs) const;

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

	private:
		enum class ItemFlags : std::uint32_t {
			None = 0,
			Directory = 0x01,
			Link = 0x02,				// Not implemented

			Encrypted = 0x10,			// Not implemented

			CompressionFlags = 0xF00,
		};

		DEATH_PRIVATE_ENUM_FLAGS(ItemFlags);

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct Item {
			Containers::String Name;
			ItemFlags Flags;
			std::uint64_t Offset;
			std::uint32_t UncompressedSize;
			std::uint32_t Size;

			Containers::Array<Item> ChildItems;
		};
#endif

		static constexpr std::uint32_t CompressionFlagsShift = 8;

		Containers::String _path;
		Containers::String _mountPoint;
		Containers::Array<Item> _rootItems;
		bool _useHashIndex;

		void ConstructsItemsFromIndex(Stream& s, Item* parentItem, bool deflateCompressed, bool useRelativeOffsets, std::uint32_t depth);
		Containers::Array<Item>* ReadIndexFromStream(Stream& s, Item* parentItem, bool useRelativeOffsets, std::int64_t indexStartPosition);
		DEATH_NEVER_INLINE Containers::Array<Item>* ReadIndexFromStreamDeflateCompressed(Stream& s, Item* parentItem, bool useRelativeOffsets, std::int64_t indexStartPosition);
		Item* FindItem(Containers::StringView path);
		Item* FindItemByHash(std::uint64_t hashedPath);

		static DEATH_ALWAYS_INLINE bool HasCompressedSize(ItemFlags itemFlags);
	};

	/**
		@brief Allows to create a `.pak` file
	*/
	class PakWriter
	{
	public:
		explicit PakWriter(Containers::StringView path, bool useHashIndex = false, bool useCompressedIndex = false, bool useRelativeOffsets = false, bool append = false);
		~PakWriter();

		PakWriter(const PakWriter&) = delete;
		PakWriter& operator=(const PakWriter&) = delete;

		explicit operator bool() const {
			return IsValid();
		}

		bool IsValid() const;

		/** @brief Adds a file to the `.pak` container */
		bool AddFile(Stream& stream, Containers::StringView path, PakPreferredCompression preferredCompression = PakPreferredCompression::None);
		/** @brief Writes file index and finalizes the `.pak` containers */
		void Finalize();

		/** @brief Returns optional mount point for the container */
		Containers::StringView GetMountPoint() const;
		/** @brief Sets optional mount point for the container */
		void SetMountPoint(Containers::String value);

	private:
		Containers::String _mountPoint;
		std::unique_ptr<FileStream> _outputStream;
		Containers::Array<PakFile::Item> _rootItems;
		bool _finalized;
		bool _alreadyExisted;
		bool _useHashIndex;
		bool _useCompressedIndex;
		bool _useRelativeOffsets;

		PakFile::Item* FindOrCreateParentItem(Containers::StringView& path);
		void WriteItemDescription(Stream& s, PakFile::Item& item, std::int64_t indexStartPosition);
	};

}}