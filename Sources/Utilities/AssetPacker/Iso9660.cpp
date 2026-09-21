#include "Iso9660.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <Containers/DateTime.h>
#include <Containers/GrowableArray.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <Core/Logger.h>
#include <IO/FileSystem.h>
#include <Utf8.h>

using namespace Death;
using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::AssetPacker
{
	namespace
	{
		/** @brief Offsets of the volume descriptor fields that say where everything is */
		enum : std::uint32_t {
			DescriptorTypeOffset = 0,
			DescriptorVersionOffset = 6,
			VolumeSpaceSizeOffset = 80,
			EscapeSequencesOffset = 88,
			PathTableSizeOffset = 132,
			PathTableLOffset = 140,
			OptionalPathTableLOffset = 144,
			PathTableMOffset = 148,
			OptionalPathTableMOffset = 152,
			RootRecordOffset = 156
		};

		/** @brief Number of bytes of a directory record that come before the name */
		constexpr std::uint32_t DirectoryRecordHeaderSize = 33;
		/** @brief Longest file identifier the generated plain hierarchy uses, version suffix included */
		constexpr std::size_t MaxIsoFileNameLength = 30;
		/** @brief Longest directory identifier the generated plain hierarchy uses */
		constexpr std::size_t MaxIsoDirectoryNameLength = 31;
		/** @brief Flag of a directory record marking the entry as a directory */
		constexpr std::uint8_t DirectoryRecordFlagDirectory = 0x02;

		std::uint32_t ReadU32LE(const std::uint8_t* source)
		{
			return std::uint32_t(source[0]) | (std::uint32_t(source[1]) << 8) |
				(std::uint32_t(source[2]) << 16) | (std::uint32_t(source[3]) << 24);
		}

		void WriteU32LE(std::uint8_t* target, std::uint32_t value)
		{
			target[0] = std::uint8_t(value);
			target[1] = std::uint8_t(value >> 8);
			target[2] = std::uint8_t(value >> 16);
			target[3] = std::uint8_t(value >> 24);
		}

		void WriteU32BE(std::uint8_t* target, std::uint32_t value)
		{
			target[0] = std::uint8_t(value >> 24);
			target[1] = std::uint8_t(value >> 16);
			target[2] = std::uint8_t(value >> 8);
			target[3] = std::uint8_t(value);
		}

		/** @brief Writes one of the numbers ISO 9660 stores in both byte orders one after the other */
		void WriteU32Both(std::uint8_t* target, std::uint32_t value)
		{
			WriteU32LE(target, value);
			WriteU32BE(target + 4, value);
		}

		void WriteU16Both(std::uint8_t* target, std::uint16_t value)
		{
			target[0] = std::uint8_t(value);
			target[1] = std::uint8_t(value >> 8);
			target[2] = std::uint8_t(value >> 8);
			target[3] = std::uint8_t(value);
		}

		std::uint32_t SectorsFor(std::uint64_t bytes)
		{
			return std::uint32_t((bytes + IsoSectorSize - 1) / IsoSectorSize);
		}

		/** @brief Turns a name into the UCS-2 big-endian form the Joliet hierarchy spells it in */
		Array<std::uint8_t> ToUcs2(StringView name, bool versioned)
		{
			Array<std::uint8_t> result;
			for (std::size_t i = 0; i < name.size(); ) {
				Pair<char32_t, std::size_t> next = Utf8::NextChar(arrayView(name.data(), name.size()), i);
				i = next.second();
				// Joliet has no room for anything outside the basic multilingual plane
				char32_t codepoint = (next.first() > 0xFFFF ? U'_' : next.first());
				arrayAppend(result, std::uint8_t(codepoint >> 8));
				arrayAppend(result, std::uint8_t(codepoint & 0xFF));
			}
			if (versioned) {
				arrayAppend(result, { std::uint8_t(0), std::uint8_t(';'), std::uint8_t(0), std::uint8_t('1') });
			}
			return result;
		}

		/** @brief Turns a name the Joliet hierarchy spells back into the form the rest of the tool works with */
		String FromUcs2(const std::uint8_t* source, std::size_t size)
		{
			Array<char> result;
			for (std::size_t i = 0; i + 1 < size; i += 2) {
				char32_t codepoint = (char32_t(source[i]) << 8) | source[i + 1];
				if (codepoint < 0x80) {
					arrayAppend(result, char(codepoint));
				} else if (codepoint < 0x800) {
					arrayAppend(result, char(0xC0 | (codepoint >> 6)));
					arrayAppend(result, char(0x80 | (codepoint & 0x3F)));
				} else {
					arrayAppend(result, char(0xE0 | (codepoint >> 12)));
					arrayAppend(result, char(0x80 | ((codepoint >> 6) & 0x3F)));
					arrayAppend(result, char(0x80 | (codepoint & 0x3F)));
				}
			}
			return String(result.data(), result.size());
		}

		/** @brief Removes the `;1` a file identifier ends with, which is not part of the name */
		StringView StripVersion(StringView name)
		{
			StringView separator = name.findLast(';');
			return (separator.empty() ? name : name.prefix(separator.begin()));
		}

		/**
			@brief Spells a name the way the plain hierarchy has to

			ISO 9660 allows upper case letters, digits and underscores and nothing else, which is why a disc
			mastered without Joliet turns `christmas-x-core_jj2.it` into `CHRISTMAS_X_CORE_JJ2.IT`. Everything
			that reads this hierarchy compares case-insensitively and stops at the version suffix.
		*/
		String ToIsoName(StringView name, bool isDirectory)
		{
			StringView baseName = name, extension = {};
			if (!isDirectory) {
				StringView separator = name.findLast('.');
				if (!separator.empty() && separator.begin() != name.begin()) {
					baseName = name.prefix(separator.begin());
					extension = name.exceptPrefix(baseName.size() + 1);
				}
			}

			// The version suffix is part of the identifier, so it has to fit in the same 30 characters
			std::size_t limit = (isDirectory ? MaxIsoDirectoryNameLength : MaxIsoFileNameLength - 2);
			if (extension.size() > 10) {
				extension = extension.prefix(10);
			}
			std::size_t baseLimit = (extension.empty() ? limit : limit - extension.size() - 1);
			if (baseName.size() > baseLimit) {
				baseName = baseName.prefix(baseLimit);
			}

			String result{NoInit, baseName.size() + (extension.empty() ? 0 : extension.size() + 1) + (isDirectory ? 0 : 2)};
			char* target = result.data();
			for (char c : baseName) {
				*target++ = ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ? c
					: (c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : '_'));
			}
			if (!extension.empty()) {
				*target++ = '.';
				for (char c : extension) {
					*target++ = ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ? c
						: (c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : '_'));
				}
			}
			if (!isDirectory) {
				*target++ = ';';
				*target++ = '1';
			}
			return result;
		}

		/** @brief Compares two identifiers the way the records of a directory have to be ordered */
		bool IsIdentifierLess(const std::uint8_t* a, std::size_t sizeA, const std::uint8_t* b, std::size_t sizeB)
		{
			std::size_t common = (sizeA < sizeB ? sizeA : sizeB);
			for (std::size_t i = 0; i < common; i++) {
				if (a[i] != b[i]) {
					return (a[i] < b[i]);
				}
			}
			// The shorter one is padded, with a character that sorts below every one an identifier may hold
			return (sizeA < sizeB);
		}

		/** @brief Fills in the 7-byte form of a date a directory record stores */
		void WriteRecordDate(std::uint8_t target[7], const DateTime& value)
		{
			if (!value.IsValid()) {
				std::memset(target, 0, 7);
				return;
			}

			target[0] = std::uint8_t(value.GetYear() - 1900);
			target[1] = std::uint8_t(value.GetMonth());
			target[2] = std::uint8_t(value.GetDay());
			target[3] = std::uint8_t(value.GetHour());
			target[4] = std::uint8_t(value.GetMinute());
			target[5] = std::uint8_t(value.GetSecond());
			// Offset from GMT in 15-minute intervals; the fields above are already local time, and nothing
			// that reads the disc does anything with the difference
			target[6] = 0;
		}

		/**
			@brief Rewrites one of the text fields of a volume descriptor in the UCS-2 a Joliet one spells

			The fields are fixed width and padded with spaces, which in UCS-2 is two bytes per space. Anything
			that no longer fits in half the room it had is cut, which is what every other tool does with them.
		*/
		void ToUcs2Field(std::uint8_t* field, std::size_t size)
		{
			std::size_t length = size;
			while (length > 0 && (field[length - 1] == ' ' || field[length - 1] == '\0')) {
				length--;
			}
			if (length > size / 2) {
				length = size / 2;
			}

			Array<std::uint8_t> converted{ValueInit, size};
			for (std::size_t i = 0; i < size / 2; i++) {
				converted[i * 2] = 0;
				converted[i * 2 + 1] = (i < length ? field[i] : ' ');
			}
			if ((size & 1) != 0) {
				converted[size - 1] = ' ';
			}
			std::memcpy(field, converted.data(), size);
		}

		/** @brief Number of bytes a directory record with an identifier of the specified length takes */
		std::uint32_t DirectoryRecordSize(std::size_t nameLength)
		{
			std::uint32_t size = DirectoryRecordHeaderSize + std::uint32_t(nameLength);
			return (size + 1) & ~std::uint32_t(1);
		}

		/** @brief Writes one directory record, returning how many bytes it took */
		std::uint32_t WriteDirectoryRecord(std::uint8_t* target, const std::uint8_t* name, std::size_t nameLength,
			std::uint32_t lba, std::uint32_t size, bool isDirectory, const std::uint8_t recorded[7])
		{
			std::uint32_t recordSize = DirectoryRecordSize(nameLength);
			std::memset(target, 0, recordSize);
			target[0] = std::uint8_t(recordSize);
			WriteU32Both(target + 2, lba);
			WriteU32Both(target + 10, size);
			std::memcpy(target + 18, recorded, 7);
			target[25] = (isDirectory ? DirectoryRecordFlagDirectory : 0);
			WriteU16Both(target + 28, 1);
			target[32] = std::uint8_t(nameLength);
			std::memcpy(target + DirectoryRecordHeaderSize, name, nameLength);
			return recordSize;
		}
	}

	bool Iso9660Reader::Open(ISectorReader& reader, std::uint32_t trackStartLba)
	{
		_reader = &reader;

		Array<std::uint8_t> sector{ValueInit, IsoSectorSize};
		if (!reader.ReadSector(trackStartLba + 16, sector.data())) {
			LOGE("Cannot read the primary volume descriptor");
			return false;
		}
		if (sector[DescriptorTypeOffset] != 1 || std::memcmp(sector.data() + 1, "CD001", 5) != 0) {
			LOGE("The data track does not carry an ISO 9660 file system");
			return false;
		}

		_primaryDescriptor = Array<std::uint8_t>{NoInit, IsoSectorSize};
		std::memcpy(_primaryDescriptor.data(), sector.data(), IsoSectorSize);
		_volumeSpaceSize = ReadU32LE(sector.data() + VolumeSpaceSizeOffset);
		_rootLba = ReadU32LE(sector.data() + RootRecordOffset + 2);
		_rootSize = ReadU32LE(sector.data() + RootRecordOffset + 10);

		// The Joliet hierarchy is what KallistiOS reads whenever a disc has one, so it is the one the names of
		// the files being carried over have to come from. It is announced by a supplementary descriptor whose
		// escape sequence names one of the three UCS-2 levels, and the console looks for it in exactly these
		// three sectors (see fs_iso9660.c), so there is no point in looking any further either
		for (std::uint32_t i = 17; i <= 19; i++) {
			if (!reader.ReadSector(trackStartLba + i, sector.data())) {
				break;
			}
			if (sector[DescriptorTypeOffset] == 255) {
				break;
			}
			if (sector[DescriptorTypeOffset] != 2 || std::memcmp(sector.data() + 1, "CD001", 5) != 0) {
				continue;
			}
			const std::uint8_t* escape = sector.data() + EscapeSequencesOffset;
			if (escape[0] == '%' && escape[1] == '/' && (escape[2] == '@' || escape[2] == 'C' || escape[2] == 'E')) {
				_supplementaryDescriptor = Array<std::uint8_t>{NoInit, IsoSectorSize};
				std::memcpy(_supplementaryDescriptor.data(), sector.data(), IsoSectorSize);
				_rootLba = ReadU32LE(sector.data() + RootRecordOffset + 2);
				_rootSize = ReadU32LE(sector.data() + RootRecordOffset + 10);
				break;
			}
		}

		return (_rootLba != 0 && _rootSize != 0);
	}

	bool Iso9660Reader::ReadDirectory(std::uint32_t lba, std::uint32_t size, SmallVectorImpl<Entry>& entries)
	{
		bool joliet = !_supplementaryDescriptor.empty();
		std::uint32_t sectorCount = SectorsFor(size);
		Array<std::uint8_t> data{ValueInit, sectorCount * IsoSectorSize};
		for (std::uint32_t i = 0; i < sectorCount; i++) {
			if (!_reader->ReadSector(lba + i, data.data() + i * IsoSectorSize)) {
				LOGE("Cannot read the directory at sector {}", lba + i);
				return false;
			}
		}

		std::uint32_t offset = 0;
		while (offset < size) {
			std::uint8_t recordSize = data[offset];
			if (recordSize == 0) {
				// A record never straddles a sector boundary, so the rest of the sector is padding
				offset = (offset / IsoSectorSize + 1) * IsoSectorSize;
				continue;
			}
			if (offset + recordSize > size || recordSize < DirectoryRecordHeaderSize) {
				break;
			}

			const std::uint8_t* record = data.data() + offset;
			offset += recordSize;

			// Every directory names itself and its parent first, with a single byte that is not a character
			const std::uint8_t* name = record + DirectoryRecordHeaderSize;
			std::size_t nameLength = record[32];
			if (nameLength == 0 || (nameLength == 1 && name[0] <= 1)) {
				continue;
			}

			String decoded = (joliet
				? FromUcs2(name, nameLength)
				: String(reinterpret_cast<const char*>(name), nameLength));

			Entry& entry = entries.emplace_back();
			entry.Name = StripVersion(decoded);
			entry.Lba = ReadU32LE(record + 2);
			entry.Size = ReadU32LE(record + 10);
			entry.IsDirectory = (record[25] & DirectoryRecordFlagDirectory) != 0;
			std::memcpy(entry.Recorded, record + 18, 7);
		}

		return true;
	}

	Iso9660Builder::Iso9660Builder()
		: _trackStartLba(0)
	{
		_root.IsDirectory = true;
		WriteRecordDate(_root.Recorded, DateTime::UtcNow());
	}

	Iso9660Builder::~Iso9660Builder()
	{
	}

	void Iso9660Builder::SetSystemArea(ArrayView<const std::uint8_t> data)
	{
		_systemArea = Array<std::uint8_t>{ValueInit, IsoSystemAreaSize};
		std::memcpy(_systemArea.data(), data.data(), (data.size() < IsoSystemAreaSize ? data.size() : IsoSystemAreaSize));
	}

	void Iso9660Builder::SetDescriptorTemplates(ArrayView<const std::uint8_t> primary, ArrayView<const std::uint8_t> supplementary)
	{
		if (primary.size() >= IsoSectorSize) {
			_primaryTemplate = Array<std::uint8_t>{NoInit, IsoSectorSize};
			std::memcpy(_primaryTemplate.data(), primary.data(), IsoSectorSize);
			// The root directory keeps the date it was given rather than being stamped with the moment the
			// volume was generated, so generating the same one twice produces the same bytes twice
			std::memcpy(_root.Recorded, _primaryTemplate.data() + RootRecordOffset + 18, 7);
		}
		if (supplementary.size() >= IsoSectorSize) {
			_supplementaryTemplate = Array<std::uint8_t>{NoInit, IsoSectorSize};
			std::memcpy(_supplementaryTemplate.data(), supplementary.data(), IsoSectorSize);
		}
	}

	Iso9660Builder::Node* Iso9660Builder::AddChild(Node& parent, StringView name, bool isDirectory)
	{
		for (auto& child : parent.Children) {
			if (StringUtils::equalsIgnoreCase(child->Name, name)) {
				return (child->IsDirectory == isDirectory ? child.get() : nullptr);
			}
		}

		auto child = std::make_unique<Node>();
		child->Name = name;
		child->IsDirectory = isDirectory;
		child->Parent = &parent;
		Node* result = child.get();
		parent.Children.push_back(std::move(child));
		return result;
	}

	Iso9660Builder::Node* Iso9660Builder::FindOrCreateDirectory(StringView path)
	{
		Node* current = &_root;
		for (StringView part : path.split('/')) {
			if (part.empty()) {
				continue;
			}
			current = AddChild(*current, part, true);
			if (current == nullptr) {
				return nullptr;
			}
		}
		return current;
	}

	bool Iso9660Builder::AddDirectory(StringView path, const std::uint8_t recorded[7])
	{
		Node* node = FindOrCreateDirectory(path);
		if (node == nullptr) {
			LOGE("Cannot put \"{}\" on the disc, a file of the same name is already there", path);
			return false;
		}
		std::memcpy(node->Recorded, recorded, 7);
		return true;
	}

	bool Iso9660Builder::AddFileFromImage(StringView path, std::uint32_t lba, std::uint32_t size, const std::uint8_t recorded[7])
	{
		StringView fileName = fs::GetFileName(path);
		Node* parent = FindOrCreateDirectory(path.prefix(fileName.begin()));
		if (parent == nullptr) {
			LOGE("Cannot put \"{}\" on the disc, a file of the same name is already there", path);
			return false;
		}

		Node* node = AddChild(*parent, fileName, false);
		if (node == nullptr) {
			LOGE("Cannot put \"{}\" on the disc, a directory of the same name is already there", path);
			return false;
		}
		node->SourceLba = lba;
		node->Size = size;
		std::memcpy(node->Recorded, recorded, 7);
		return true;
	}

	bool Iso9660Builder::AddDirectoryFromDisk(StringView path, StringView hostPath)
	{
		Node* node = FindOrCreateDirectory(path);
		if (node == nullptr) {
			LOGE("Cannot put \"{}\" on the disc, a file of the same name is already there", path);
			return false;
		}
		WriteRecordDate(node->Recorded, fs::GetLastModificationTime(hostPath));
		return AddDirectoryContents(*node, hostPath);
	}

	bool Iso9660Builder::AddDirectoryContents(Node& parent, StringView hostPath)
	{
		// Anything that cannot be put on the disc stops the whole thing, because a disc is read-only and a
		// game missing one of its files is worse than no disc at all
		for (auto item : fs::Directory(hostPath)) {
			StringView itemName = fs::GetFileName(item);
			bool isDirectory = fs::DirectoryExists(item);

			Node* node = AddChild(parent, itemName, isDirectory);
			if (node == nullptr) {
				LOGE("Cannot put \"{}\" on the disc, something of the same name is already there --- names "
					"that differ only in case name the same file to everything that reads a disc", item);
				return false;
			}
			WriteRecordDate(node->Recorded, fs::GetLastModificationTime(item));

			if (isDirectory) {
				String itemPath = item;
				if (!AddDirectoryContents(*node, itemPath)) {
					return false;
				}
				continue;
			}

			std::int64_t size = fs::GetFileSize(item);
			if (size < 0 || size > std::int64_t(UINT32_MAX)) {
				LOGE("Cannot put \"{}\" on the disc, it is unreadable or larger than 4 GB", item);
				return false;
			}
			node->HostPath = item;
			node->Size = std::uint32_t(size);
		}
		return true;
	}

	void Iso9660Builder::AssignNames(Node& node)
	{
		// Ordered by the name they were added under first, so that the numbering below, and with it the whole
		// volume, comes out the same however the file system happened to hand the entries over
		std::sort(node.Children.begin(), node.Children.end(), [](const std::unique_ptr<Node>& a, const std::unique_ptr<Node>& b) {
			return IsIdentifierLess(reinterpret_cast<const std::uint8_t*>(a->Name.data()), a->Name.size(),
				reinterpret_cast<const std::uint8_t*>(b->Name.data()), b->Name.size());
		});

		for (auto& child : node.Children) {
			child->IsoName = ToIsoName(child->Name, child->IsDirectory);
			child->JolietName = ToUcs2(child->Name, !child->IsDirectory);
		}

		// Two names that differ only in something the plain hierarchy cannot spell collapse onto each other,
		// which would leave the disc with two records of the same identifier. The Joliet hierarchy keeps the
		// two names apart, so only the plain one has to be made unique again
		for (std::size_t i = 1; i < node.Children.size(); i++) {
			for (std::size_t attempt = 0; attempt < node.Children.size(); attempt++) {
				bool taken = false;
				for (std::size_t j = 0; j < i; j++) {
					if (StringUtils::equalsIgnoreCase(node.Children[i]->IsoName, node.Children[j]->IsoName)) {
						taken = true;
						break;
					}
				}
				if (!taken) {
					break;
				}

				String& name = node.Children[i]->IsoName;
				char suffix[8];
				std::size_t suffixLength = std::size_t(std::snprintf(suffix, sizeof(suffix), "%zu", attempt));
				std::size_t versionLength = (node.Children[i]->IsDirectory ? 0 : 2);
				std::size_t at = (name.size() > versionLength + suffixLength ? name.size() - versionLength - suffixLength : 0);
				std::memcpy(name.data() + at, suffix, (suffixLength < name.size() ? suffixLength : name.size()));
			}
		}

		std::sort(node.Children.begin(), node.Children.end(), [](const std::unique_ptr<Node>& a, const std::unique_ptr<Node>& b) {
			return IsIdentifierLess(reinterpret_cast<const std::uint8_t*>(a->IsoName.data()), a->IsoName.size(),
				reinterpret_cast<const std::uint8_t*>(b->IsoName.data()), b->IsoName.size());
		});

		node.JolietChildren.clear();
		for (auto& child : node.Children) {
			node.JolietChildren.push_back(child.get());
		}
		std::sort(node.JolietChildren.begin(), node.JolietChildren.end(), [](Node* a, Node* b) {
			return IsIdentifierLess(a->JolietName.data(), a->JolietName.size(), b->JolietName.data(), b->JolietName.size());
		});

		for (auto& child : node.Children) {
			if (child->IsDirectory) {
				AssignNames(*child);
			}
		}
	}

	bool Iso9660Builder::BuildLayout(std::uint32_t volumeSectorCount, Layout& layout)
	{
		AssignNames(_root);

		// The path tables list every directory by level and, within a level, in the order its parent lists it,
		// so walking the hierarchy breadth-first over the already sorted children produces exactly that order
		_root.PathTableIndex = 1;
		_root.ParentPathTableIndex = 1;
		layout.Directories.push_back(&_root);
		for (std::size_t i = 0; i < layout.Directories.size(); i++) {
			Node* directory = layout.Directories[i];
			for (auto& child : directory->Children) {
				if (!child->IsDirectory) {
					continue;
				}
				child->PathTableIndex = std::uint32_t(layout.Directories.size() + 1);
				child->ParentPathTableIndex = directory->PathTableIndex;
				layout.Directories.push_back(child.get());
			}
		}
		if (layout.Directories.size() > UINT16_MAX) {
			LOGE("The disc cannot hold more than {} directories", UINT16_MAX);
			return false;
		}

		// Both hierarchies need an extent per directory, holding the two records every directory starts with
		// and one per entry, and a path table record naming it
		for (Node* directory : layout.Directories) {
			std::uint32_t size = 2 * DirectoryRecordSize(1);
			std::uint32_t jolietSize = size;
			std::size_t pathTableName = (directory == &_root ? 1 : directory->IsoName.size());
			std::size_t jolietPathTableName = (directory == &_root ? 1 : directory->Name.size() * 2);
			layout.PathTableSize += std::uint32_t(8 + pathTableName + (pathTableName & 1));
			layout.JolietPathTableSize += std::uint32_t(8 + jolietPathTableName + (jolietPathTableName & 1));

			for (auto& child : directory->Children) {
				std::uint32_t recordSize = DirectoryRecordSize(child->IsoName.size());
				if (recordSize > IsoSectorSize) {
					LOGE("\"{}\" has a name too long to be put on a disc", child->Name);
					return false;
				}
				// A record is never allowed to straddle a sector boundary
				if ((size % IsoSectorSize) + recordSize > IsoSectorSize) {
					size = SectorsFor(size) * IsoSectorSize;
				}
				size += recordSize;
			}
			for (Node* child : directory->JolietChildren) {
				std::uint32_t recordSize = DirectoryRecordSize(child->JolietName.size());
				if (recordSize > IsoSectorSize) {
					LOGE("\"{}\" has a name too long to be put on a disc", child->Name);
					return false;
				}
				if ((jolietSize % IsoSectorSize) + recordSize > IsoSectorSize) {
					jolietSize = SectorsFor(jolietSize) * IsoSectorSize;
				}
				jolietSize += recordSize;
			}

			directory->ExtentSize = SectorsFor(size) * IsoSectorSize;
			directory->JolietExtentSize = SectorsFor(jolietSize) * IsoSectorSize;
		}

		// The volume descriptors, the four path tables and then the two sets of directory extents; the files
		// go at the very end of the volume, so everything up to here is all that has a fixed place
		std::uint32_t sector = 19;
		layout.PathTableSectors = SectorsFor(layout.PathTableSize);
		layout.JolietPathTableSectors = SectorsFor(layout.JolietPathTableSize);
		layout.PathTableLba = sector;
		sector += 2 * layout.PathTableSectors;
		layout.JolietPathTableLba = sector;
		sector += 2 * layout.JolietPathTableSectors;

		for (Node* directory : layout.Directories) {
			directory->Lba = _trackStartLba + sector;
			sector += directory->ExtentSize / IsoSectorSize;
		}
		for (Node* directory : layout.Directories) {
			directory->JolietLba = _trackStartLba + sector;
			sector += directory->JolietExtentSize / IsoSectorSize;
		}

		// Files in the order the directories list them, which keeps everything one directory holds together
		std::uint32_t fileSectors = 0;
		for (std::size_t i = 0; i < layout.Directories.size(); i++) {
			for (auto& child : layout.Directories[i]->Children) {
				if (child->IsDirectory) {
					continue;
				}
				layout.Files.push_back(child.get());
				fileSectors += SectorsFor(child->Size);
			}
		}

		if (volumeSectorCount == 0) {
			volumeSectorCount = sector + fileSectors;
		} else if (sector + fileSectors > volumeSectorCount) {
			return false;
		}

		layout.VolumeSectorCount = volumeSectorCount;
		layout.FirstFileLba = _trackStartLba + volumeSectorCount - fileSectors;

		std::uint32_t fileLba = layout.FirstFileLba;
		for (Node* file : layout.Files) {
			file->Lba = fileLba;
			fileLba += SectorsFor(file->Size);
		}

		return true;
	}

	std::uint32_t Iso9660Builder::GetRequiredSectorCount()
	{
		Layout layout;
		if (!BuildLayout(0, layout)) {
			return 0;
		}
		return layout.VolumeSectorCount;
	}

	void Iso9660Builder::WriteDescriptors(ArrayView<std::uint8_t> target, const Layout& layout)
	{
		std::uint8_t rootName = 0;

		std::uint8_t* primary = target.data();
		if (!_primaryTemplate.empty()) {
			std::memcpy(primary, _primaryTemplate.data(), IsoSectorSize);
		}
		WriteU32Both(primary + VolumeSpaceSizeOffset, layout.VolumeSectorCount);
		WriteU32Both(primary + PathTableSizeOffset, layout.PathTableSize);
		WriteU32LE(primary + PathTableLOffset, _trackStartLba + layout.PathTableLba);
		WriteU32LE(primary + OptionalPathTableLOffset, 0);
		WriteU32BE(primary + PathTableMOffset, _trackStartLba + layout.PathTableLba + layout.PathTableSectors);
		WriteU32BE(primary + OptionalPathTableMOffset, 0);
		WriteDirectoryRecord(primary + RootRecordOffset, &rootName, 1, _root.Lba, _root.ExtentSize, true, _root.Recorded);

		std::uint8_t* supplementary = target.data() + IsoSectorSize;
		if (!_supplementaryTemplate.empty()) {
			std::memcpy(supplementary, _supplementaryTemplate.data(), IsoSectorSize);
		} else {
			// With none to copy, the primary descriptor is the next best thing: the same volume, described
			// again, with its text spelled in UCS-2 and the escape sequence that says so. This is the path a
			// disc mastered without Joliet takes --- the PlayStation 2 one, whose driver reads a Joliet
			// hierarchy and ignores the Rock Ridge names such a disc carries instead
			std::memcpy(supplementary, primary, IsoSectorSize);
			supplementary[DescriptorTypeOffset] = 2;
			supplementary[EscapeSequencesOffset + 0] = '%';
			supplementary[EscapeSequencesOffset + 1] = '/';
			supplementary[EscapeSequencesOffset + 2] = 'E';
			static const std::uint32_t textFields[][2] = {
				{ 8, 32 }, { 40, 32 }, { 190, 128 }, { 318, 128 }, { 446, 128 },
				{ 574, 128 }, { 702, 37 }, { 739, 37 }, { 776, 37 }
			};
			for (const std::uint32_t(&field)[2] : textFields) {
				ToUcs2Field(supplementary + field[0], field[1]);
			}
		}
		WriteU32Both(supplementary + VolumeSpaceSizeOffset, layout.VolumeSectorCount);
		WriteU32Both(supplementary + PathTableSizeOffset, layout.JolietPathTableSize);
		WriteU32LE(supplementary + PathTableLOffset, _trackStartLba + layout.JolietPathTableLba);
		WriteU32LE(supplementary + OptionalPathTableLOffset, 0);
		WriteU32BE(supplementary + PathTableMOffset, _trackStartLba + layout.JolietPathTableLba + layout.JolietPathTableSectors);
		WriteU32BE(supplementary + OptionalPathTableMOffset, 0);
		WriteDirectoryRecord(supplementary + RootRecordOffset, &rootName, 1, _root.JolietLba, _root.JolietExtentSize, true, _root.Recorded);

		std::uint8_t* terminator = target.data() + 2 * IsoSectorSize;
		terminator[DescriptorTypeOffset] = 255;
		std::memcpy(terminator + 1, "CD001", 5);
		terminator[DescriptorVersionOffset] = 1;
	}

	void Iso9660Builder::WritePathTables(ArrayView<std::uint8_t> target, const Layout& layout)
	{
		const std::uint8_t rootName = 0;

		for (std::int32_t pass = 0; pass < 4; pass++) {
			bool joliet = (pass >= 2);
			bool bigEndian = (pass & 1) != 0;
			std::uint32_t sectors = (joliet ? layout.JolietPathTableSectors : layout.PathTableSectors);
			std::uint32_t lba = (joliet ? layout.JolietPathTableLba : layout.PathTableLba) + (bigEndian ? sectors : 0);
			std::uint8_t* record = target.data() + (lba - 16) * IsoSectorSize;

			for (Node* directory : layout.Directories) {
				const std::uint8_t* name;
				std::size_t nameLength;
				if (directory == &_root) {
					name = &rootName;
					nameLength = 1;
				} else if (joliet) {
					// A directory has no version suffix, so its Joliet identifier is the name and nothing else
					name = directory->JolietName.data();
					nameLength = directory->JolietName.size();
				} else {
					name = reinterpret_cast<const std::uint8_t*>(directory->IsoName.data());
					nameLength = directory->IsoName.size();
				}

				record[0] = std::uint8_t(nameLength);
				record[1] = 0;
				std::uint32_t extent = (joliet ? directory->JolietLba : directory->Lba);
				std::uint16_t parent = std::uint16_t(directory->ParentPathTableIndex);
				if (bigEndian) {
					WriteU32BE(record + 2, extent);
					record[6] = std::uint8_t(parent >> 8);
					record[7] = std::uint8_t(parent);
				} else {
					WriteU32LE(record + 2, extent);
					record[6] = std::uint8_t(parent);
					record[7] = std::uint8_t(parent >> 8);
				}
				std::memcpy(record + 8, name, nameLength);
				if ((nameLength & 1) != 0) {
					record[8 + nameLength] = 0;
				}
				record += 8 + nameLength + (nameLength & 1);
			}
		}
	}

	void Iso9660Builder::WriteDirectoryExtents(ArrayView<std::uint8_t> target, const Layout& layout, bool joliet)
	{
		const std::uint8_t selfName[] = { 0, 1 };

		for (Node* directory : layout.Directories) {
			Node* parent = (directory->Parent != nullptr ? directory->Parent : directory);
			std::uint32_t lba = (joliet ? directory->JolietLba : directory->Lba) - _trackStartLba;
			std::uint32_t extentSize = (joliet ? directory->JolietExtentSize : directory->ExtentSize);
			std::uint8_t* extent = target.data() + (lba - 16) * IsoSectorSize;

			// Every directory names itself and its parent first, whatever else it holds
			std::uint32_t offset = WriteDirectoryRecord(extent, &selfName[0], 1,
				(joliet ? directory->JolietLba : directory->Lba), extentSize, true, directory->Recorded);
			offset += WriteDirectoryRecord(extent + offset, &selfName[1], 1,
				(joliet ? parent->JolietLba : parent->Lba), (joliet ? parent->JolietExtentSize : parent->ExtentSize),
				true, parent->Recorded);

			// The two hierarchies spell the same names differently, so their records are not in the same order
			std::size_t childCount = directory->Children.size();
			for (std::size_t i = 0; i < childCount; i++) {
				Node* child = (joliet ? directory->JolietChildren[i] : directory->Children[i].get());
				const std::uint8_t* name;
				std::size_t nameLength;
				if (joliet) {
					name = child->JolietName.data();
					nameLength = child->JolietName.size();
				} else {
					name = reinterpret_cast<const std::uint8_t*>(child->IsoName.data());
					nameLength = child->IsoName.size();
				}

				if ((offset % IsoSectorSize) + DirectoryRecordSize(nameLength) > IsoSectorSize) {
					offset = SectorsFor(offset) * IsoSectorSize;
				}
				offset += WriteDirectoryRecord(extent + offset, name, nameLength,
					(child->IsDirectory ? (joliet ? child->JolietLba : child->Lba) : child->Lba),
					(child->IsDirectory ? (joliet ? child->JolietExtentSize : child->ExtentSize) : child->Size),
					child->IsDirectory, child->Recorded);
			}

			DEATH_ASSERT(offset <= extentSize, "Directory extent overflowed", );
		}
	}

	bool Iso9660Builder::Write(ISectorSink& sink, std::uint32_t volumeSectorCount, ISectorReader& imageReader)
	{
		Layout layout;
		if (!BuildLayout(volumeSectorCount, layout)) {
			LOGE("The file system does not fit in {} sectors", volumeSectorCount);
			return false;
		}

		// The system area, which on a Dreamcast disc holds the bootstrap the drive reads before anything else
		Array<std::uint8_t> sector{ValueInit, IsoSectorSize};
		for (std::uint32_t i = 0; i < 16; i++) {
			if (!sink.WriteSector(_systemArea.empty() ? sector.data() : _systemArea.data() + i * IsoSectorSize)) {
				return false;
			}
		}

		// Everything between the descriptors and the files is small enough to lay out in one go
		Node* lastDirectory = layout.Directories.back();
		std::uint32_t metadataEnd = (lastDirectory->JolietLba - _trackStartLba) + lastDirectory->JolietExtentSize / IsoSectorSize;
		Array<std::uint8_t> metadata{ValueInit, (metadataEnd - 16) * IsoSectorSize};
		WriteDescriptors(metadata, layout);
		WritePathTables(metadata, layout);
		WriteDirectoryExtents(metadata, layout, false);
		WriteDirectoryExtents(metadata, layout, true);

		for (std::uint32_t i = 16; i < metadataEnd; i++) {
			if (!sink.WriteSector(metadata.data() + (i - 16) * IsoSectorSize)) {
				return false;
			}
		}

		// The gap that pushes the files out to the edge of the disc
		std::memset(sector.data(), 0, IsoSectorSize);
		for (std::uint32_t i = metadataEnd; i < layout.FirstFileLba - _trackStartLba; i++) {
			if (!sink.WriteSector(sector.data())) {
				return false;
			}
		}

		for (Node* file : layout.Files) {
			std::unique_ptr<Stream> s;
			if (!file->HostPath.empty()) {
				s = fs::Open(file->HostPath, FileAccess::Read);
				if (!s->IsValid()) {
					LOGE("Cannot open \"{}\"", file->HostPath);
					return false;
				}
			}

			std::uint32_t sectorCount = SectorsFor(file->Size);
			for (std::uint32_t i = 0; i < sectorCount; i++) {
				std::uint32_t remaining = file->Size - i * IsoSectorSize;
				std::uint32_t chunk = (remaining < IsoSectorSize ? remaining : IsoSectorSize);
				std::memset(sector.data(), 0, IsoSectorSize);
				if (s != nullptr) {
					if (s->Read(sector.data(), chunk) != chunk) {
						LOGE("Cannot read \"{}\"", file->HostPath);
						return false;
					}
				} else if (!imageReader.ReadSector(file->SourceLba + i, sector.data())) {
					LOGE("Cannot read \"{}\" from the disc image", file->Name);
					return false;
				}
				if (!sink.WriteSector(sector.data())) {
					return false;
				}
			}
		}

		// Whatever is left over, which a volume that was given more sectors than it needs ends with
		std::memset(sector.data(), 0, IsoSectorSize);
		std::uint32_t written = layout.FirstFileLba - _trackStartLba;
		for (Node* file : layout.Files) {
			written += SectorsFor(file->Size);
		}
		for (std::uint32_t i = written; i < layout.VolumeSectorCount; i++) {
			if (!sink.WriteSector(sector.data())) {
				return false;
			}
		}

		return true;
	}
}
