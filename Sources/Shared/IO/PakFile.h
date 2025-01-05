#pragma once

#include "../Common.h"
#include "../Containers/Array.h"
#include "../Containers/String.h"
#include "FileStream.h"
#include "FileSystem.h"

#include <memory>

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/** @brief Preferred file compression, used in @ref PakWritter::AddFile() */
	enum class PakPreferredCompression
	{
		None,
		Deflate,
		Lz4,
		Zstd
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

		Containers::StringView GetMountPoint() const;
		Containers::StringView GetPath() const;
		bool IsValid() const;

		bool FileExists(Containers::StringView path);
		bool DirectoryExists(Containers::StringView path);

		std::unique_ptr<Stream> OpenFile(Containers::StringView path);

		/** @brief Handles directory traversal, should be used as iterator */
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
			ZlibCompressed = 0x02,
			Lz4Compressed = 0x04,
			ZstdCompressed = 0x08,

			Lzma2Compressed = 0x10,		// Not implemented
			Aes256Encrypten = 0x40,		// Not implemented

			Link = 0x80					// Not implemented
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

		static constexpr std::uint64_t Signature = 0x208FA69FF0BFBBEF;
		static constexpr std::uint16_t Version = 1;

		Containers::String _path;
		Containers::String _mountPoint;
		Containers::Array<Item> _rootItems;

		void ReadIndex(std::unique_ptr<Stream>& s, Item* parentItem);

		Item* FindItem(Containers::StringView path);

		static DEATH_ALWAYS_INLINE bool HasCompressedSize(ItemFlags itemFlags);
	};

	/**
		@brief Allows to create a `.pak` file
	*/
	class PakWriter
	{
	public:
		Containers::String MountPoint;

		explicit PakWriter(Containers::StringView path);
		~PakWriter();

		PakWriter(const PakWriter&) = delete;
		PakWriter& operator=(const PakWriter&) = delete;

		bool IsValid() const;

		bool AddFile(Stream& stream, Containers::StringView path, PakPreferredCompression preferredCompression = PakPreferredCompression::None);
		void Finalize();

	private:
		std::unique_ptr<FileStream> _outputStream;
		Containers::Array<PakFile::Item> _rootItems;
		bool _finalized;

		PakFile::Item* FindOrCreateParentItem(Containers::StringView& path);
		void WriteItemDescription(PakFile::Item& item);
	};

}}