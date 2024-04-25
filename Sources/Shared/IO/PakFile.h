#pragma once

#include "../Common.h"
#include "../Containers/Array.h"
#include "../Containers/String.h"
#include "FileStream.h"

#include <cstdio>		// For FILE
#include <memory>

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	class PakFile
	{
		friend class PakWriter;

	public:
		explicit PakFile(const Containers::StringView path);

		PakFile(const PakFile&) = delete;
		PakFile& operator=(const PakFile&) = delete;

		Containers::StringView GetMountPoint() const;
		Containers::StringView GetPath() const;
		bool IsValid() const;

		std::unique_ptr<Stream> OpenFile(const Containers::StringView path);

	protected:
		enum class ItemFlags : std::uint32_t {
			None = 0,
			Directory = 0x01,
			ZlibCompressed = 0x02
		};

		DEFINE_PRIVATE_ENUM_OPERATORS(ItemFlags);

		struct Item {
			Containers::String Name;
			ItemFlags Flags;
			std::uint64_t Offset;
			std::uint32_t UncompressedSize;
			std::uint32_t Size;

			Containers::Array<Item> ChildItems;
		};

		static constexpr std::uint64_t Signature = 0x208FA69FF0BFBBEF;
		static constexpr std::uint16_t Version = 1;

		Containers::String _path;
		Containers::String _mountPoint;
		Containers::Array<Item> _rootItems;

		void ReadIndex(std::unique_ptr<Stream>& s, Item* parentItem);

		Item* FindItem(Containers::StringView path);
	};

	class PakWriter
	{
	public:
		Containers::String MountPoint;

		explicit PakWriter(const Containers::StringView path);
		~PakWriter();

		PakWriter(const PakWriter&) = delete;
		PakWriter& operator=(const PakWriter&) = delete;

		bool IsValid() const;

		bool AddFile(Stream& stream, Containers::StringView path, bool compress = false);
		void Finalize();

	private:
		std::unique_ptr<FileStream> _outputStream;
		Containers::Array<PakFile::Item> _rootItems;
		bool _finalized;

		PakFile::Item* FindOrCreateParentItem(Containers::StringView& path);
		void WriteItemDescription(PakFile::Item& item);
	};

}}