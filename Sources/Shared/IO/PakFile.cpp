#include "PakFile.h"
#include "BoundedFileStream.h"
#include "FileSystem.h"
#include "Compression/DeflateStream.h"
#include "Compression/Lz4Stream.h"
#include "Compression/ZstdStream.h"
#include "../Base/Memory.h"
#include "../Containers/GrowableArray.h"
#include "../Containers/SmallVector.h"
#include "../Cryptography/xxHash.h"

#include <algorithm>

using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::Cryptography;

enum class PakFileFlags : std::uint16_t {
	None = 0x00,
	DeflateCompressedIndex = 0x01,
	Aes256EncryptedIndex = 0x02,
	HashIndex = 0x04,
	RelativeOffsets = 0x08
};

DEATH_ENUM_FLAGS(PakFileFlags);

static constexpr std::uint8_t OptionalHeader[] = { 0xEF, 0xBB, 0xBF, 0xF0, 0x9F, 0x8C, 0xAA, 0x3A };
static constexpr std::uint8_t Signature[] = { 0xF0, 0x9F, 0x8C, 0xAA };
static constexpr std::uint16_t Version = 1;
static constexpr std::int32_t FooterSize = sizeof(Signature) + 2 + 2 + 8;
static constexpr std::uint32_t MaxDepth = 64;
static constexpr std::uint32_t HashIndexLength = 8;

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	static std::uint64_t FileNameToHash(StringView fileName)
	{
		SmallVector<char, 512> normalizedFileName(DefaultInit, fileName.size());
		bool lastWasSlash = false;
		std::size_t i = 0;

		for (char c : fileName) {
			// Normalize slashes
			if (c == '\\') {
				c = '/';
			}

			// Skip consecutive slashes
			if (c == '/') {
				if (lastWasSlash) continue;
				lastWasSlash = true;
			} else {
				lastWasSlash = false;
			}

			// Uppercase to lowercase, copied from StringUtils::lowercaseInPlace()
			c += (std::uint8_t(c - 'A') < 26) << 5;
			normalizedFileName[i++] = c;
		}

		return xxHash3(normalizedFileName.data(), i);
	}

	static std::int64_t FindRelocatedFooter(std::unique_ptr<Death::IO::Stream>& s)
	{
		s->Seek(0, SeekOrigin::Begin);

		std::uint32_t magic = s->ReadValueAsLE<std::uint32_t>();

		// Check for PE signature (MZ header)
		if ((magic & 0xFFFF) == 0x5A4D) { // "MZ"
			s->Seek(0x3C, SeekOrigin::Begin);
			std::uint32_t peHeaderOffset = s->ReadValueAsLE<std::uint32_t>();

			s->Seek(peHeaderOffset, SeekOrigin::Begin);
			std::uint32_t peSignature = s->ReadValueAsLE<std::uint32_t>();
			if (peSignature != 0x00004550) { // "PE\0\0"
				return -1;
			}

			// Read COFF header
			s->Seek(peHeaderOffset + 6, SeekOrigin::Begin);
			std::uint16_t numberOfSections = s->ReadValueAsLE<std::uint16_t>();
			s->Seek(12, SeekOrigin::Current); // Skip timestamp, symbol table pointer, number of symbols
			std::uint16_t sizeOfOptionalHeader = s->ReadValueAsLE<std::uint16_t>();
			s->Seek(2, SeekOrigin::Current); // Skip characteristics

			// Skip optional header to get to section table
			std::int64_t sectionTablePos = s->GetPosition() + sizeOfOptionalHeader;

			// Search for "pak" section
			for (std::uint16_t i = 0; i < numberOfSections; i++) {
				s->Seek(sectionTablePos + i * 40, SeekOrigin::Begin);

				char sectionName[9];
				s->Read(sectionName, 8);
				sectionName[8] = '\0';

				if (std::memcmp(sectionName, "pak", 4) == 0) {
					// Found pak section, read its file offset and size
					s->Seek(8, SeekOrigin::Current); // Skip VirtualSize and VirtualAddress
					std::uint32_t sizeOfRawData = s->ReadValueAsLE<std::uint32_t>();
					std::uint32_t pointerToRawData = s->ReadValueAsLE<std::uint32_t>();

					if (sizeOfRawData >= FooterSize && pointerToRawData > 0) {
						// Calculate footer position (at the end of pak section)
						std::int64_t footerPosition = std::int64_t(pointerToRawData) + std::int64_t(sizeOfRawData) - FooterSize;
						
						s->Seek(footerPosition, SeekOrigin::Begin);
						std::uint8_t signature[sizeof(Signature)];
						s->Read(signature, sizeof(signature));

						if (std::memcmp(signature, Signature, sizeof(Signature)) == 0) {
							return footerPosition;
						}
					}
					break;
				}
			}

			return -1;
		}

		// Check for ELF signature
		if (magic == 0x464C457F) { // "\x7FELF"
			// Read ELF class (32-bit or 64-bit)
			s->Seek(4, SeekOrigin::Begin);
			std::uint8_t elfClass = s->ReadValue<std::uint8_t>(); // 1 = 32-bit, 2 = 64-bit
			std::uint8_t elfData = s->ReadValue<std::uint8_t>(); // 1 = little endian, 2 = big endian
			bool is64Bit = (elfClass == 2);
			bool isBigEndian = (elfData == 2);

			// Helper lambdas for endianness-aware reading
			auto readU16 = [&]() -> std::uint16_t {
				std::uint16_t value = s->ReadValueAsLE<std::uint16_t>();
				if DEATH_UNLIKELY(isBigEndian) {
					value = Memory::SwapBytes(value);
				}
				return value;
			};

			auto readU32 = [&]() -> std::uint32_t {
				std::uint32_t value = s->ReadValueAsLE<std::uint32_t>();
				if DEATH_UNLIKELY(isBigEndian) {
					value = Memory::SwapBytes(value);
				}
				return value;
			};

			auto readU64 = [&]() -> std::uint64_t {
				std::uint64_t value = s->ReadValueAsLE<std::uint64_t>();
				if DEATH_UNLIKELY(isBigEndian) {
					value = Memory::SwapBytes(value);
				}
				return value;
			};

			// Get section header table info
			std::int64_t sectionTablePos, sectionHeaderSize;
			std::uint16_t numSections, stringSectionIdx;
			if (is64Bit) {
				sectionHeaderSize = 64;
				s->Seek(40, SeekOrigin::Begin);
				sectionTablePos = readU64();
				s->Seek(60, SeekOrigin::Begin);
				numSections = readU16();
				stringSectionIdx = readU16();
			} else {
				sectionHeaderSize = 40;
				s->Seek(32, SeekOrigin::Begin);
				sectionTablePos = readU32();
				s->Seek(48, SeekOrigin::Begin);
				numSections = readU16();
				stringSectionIdx = readU16();
			}

			// Read string table
			s->Seek(sectionTablePos + stringSectionIdx * sectionHeaderSize, SeekOrigin::Begin);

			std::int64_t stringDataPos, stringDataSize;
			if (is64Bit) {
				s->Seek(24, SeekOrigin::Current);
				stringDataPos = readU64();
				stringDataSize = readU64();
			} else {
				s->Seek(16, SeekOrigin::Current);
				stringDataPos = readU32();
				stringDataSize = readU32();
			}

			// Read string table data
			s->Seek(stringDataPos, SeekOrigin::Begin);
			Array<std::uint8_t> strings(NoInit, stringDataSize);
			s->Read(strings.data(), stringDataSize);

			// Search for "pak" section
			for (std::uint16_t i = 0; i < numSections; i++) {
				std::int64_t sectionHeaderPos = sectionTablePos + i * sectionHeaderSize;
				s->Seek(sectionHeaderPos, SeekOrigin::Begin);

				std::uint32_t nameOffset = readU32();
				if (nameOffset < stringDataSize) {
					const char* sectionName = reinterpret_cast<const char*>(strings.data() + nameOffset);
					if (std::strcmp(sectionName, "pak") == 0) {
						// Found pak section, read its offset and size
						std::uint64_t offset, size;
						if (is64Bit) {
							s->Seek(sectionHeaderPos + 24, SeekOrigin::Begin);
							offset = readU64();
							size = readU64();
						} else {
							s->Seek(sectionHeaderPos + 16, SeekOrigin::Begin);
							offset = readU32();
							size = readU32();
						}

						if (size >= FooterSize && offset > 0) {
							// Calculate footer position (at the end of pak section)
							std::int64_t footerPosition = std::int64_t(offset) + std::int64_t(size) - FooterSize;
							
							s->Seek(footerPosition, SeekOrigin::Begin);
							std::uint8_t signature[sizeof(Signature)];
							s->Read(signature, sizeof(signature));
							
							if (std::memcmp(signature, Signature, sizeof(Signature)) == 0) {
								return footerPosition;
							}
						}
						break;
					}
				}
			}

			return -1;
		}

		// Unknown file format
		return -1;
	}
	
#if defined(WITH_ZLIB) || defined(WITH_MINIZ) || defined(WITH_LZ4) || defined(WITH_ZSTD)

	using namespace Death::IO::Compression;

	template<class T>
	class CompressedBoundedStream : public Stream
	{
	public:
		CompressedBoundedStream(StringView path, std::uint64_t offset, std::uint32_t uncompressedSize, std::uint32_t compressedSize, std::int32_t bufferSize);

		CompressedBoundedStream(const CompressedBoundedStream&) = delete;
		CompressedBoundedStream& operator=(const CompressedBoundedStream&) = delete;

		void Dispose() override;
		std::int64_t Seek(std::int64_t offset, SeekOrigin origin) override;
		std::int64_t GetPosition() const override;
		std::int64_t Read(void* destination, std::int64_t bytesToRead) override;
		std::int64_t Write(const void* source, std::int64_t bytesToWrite) override;
		bool Flush() override;
		bool IsValid() override;
		std::int64_t GetSize() const override;
		std::int64_t SetSize(std::int64_t size) override;

	private:
		BoundedFileStream _underlyingStream;
		T _compressedStream;
		std::int64_t _uncompressedSize;
	};

	template<class T>
	CompressedBoundedStream<T>::CompressedBoundedStream(StringView path, std::uint64_t offset, std::uint32_t uncompressedSize, std::uint32_t compressedSize, std::int32_t bufferSize)
		: _underlyingStream(path, offset, compressedSize, bufferSize), _uncompressedSize(uncompressedSize)
	{
		_compressedStream.Open(_underlyingStream, static_cast<std::int32_t>(compressedSize));
	}

	template<class T>
	void CompressedBoundedStream<T>::Dispose()
	{
		_compressedStream.Dispose();
		_underlyingStream.Dispose();
	}

	template<class T>
	std::int64_t CompressedBoundedStream<T>::Seek(std::int64_t offset, SeekOrigin origin)
	{
		return _compressedStream.Seek(offset, origin);
	}

	template<class T>
	std::int64_t CompressedBoundedStream<T>::GetPosition() const
	{
		return _compressedStream.GetPosition();
	}

	template<class T>
	std::int64_t CompressedBoundedStream<T>::Read(void* destination, std::int64_t bytesToRead)
	{
		return _compressedStream.Read(destination, bytesToRead);
	}

	template<class T>
	std::int64_t CompressedBoundedStream<T>::Write(const void* source, std::int64_t bytesToWrite)
	{
		// Not supported
		return Stream::Invalid;
	}

	template<class T>
	bool CompressedBoundedStream<T>::Flush()
	{
		// Not supported
		return true;
	}

	template<class T>
	bool CompressedBoundedStream<T>::IsValid()
	{
		return _underlyingStream.IsValid() && _compressedStream.IsValid();
	}

	template<class T>
	std::int64_t CompressedBoundedStream<T>::GetSize() const
	{
		return _uncompressedSize;
	}

	template<class T>
	std::int64_t CompressedBoundedStream<T>::SetSize(std::int64_t size)
	{
		return Stream::Invalid;
	}

#	if defined(WITH_ZLIB) || defined(WITH_MINIZ)
	static void CopyToDeflate(Stream& input, Stream& output, std::int64_t& uncompressedSize)
	{
		DeflateWriter dw(output);
		uncompressedSize = input.CopyTo(dw);
	}
#	endif
#	if defined(WITH_LZ4)
	static void CopyToLz4(Stream& input, Stream& output, std::int64_t& uncompressedSize)
	{
		Lz4Writer dw(output);
		uncompressedSize = input.CopyTo(dw);
	}
#	endif
#	if defined(WITH_ZSTD)
	static void CopyToZstd(Stream& input, Stream& output, std::int64_t& uncompressedSize)
	{
		ZstdWriter dw(output);
		uncompressedSize = input.CopyTo(dw);
	}
#	endif
#endif

	PakFile::PakFile(StringView path)
	{
		std::unique_ptr<Stream> s = std::make_unique<FileStream>(path, FileAccess::Read);
		DEATH_ASSERT(s->GetSize() > FooterSize + 8, "Invalid .pak file", );

		// Header size is 18 bytes
		std::int64_t footerPosition = s->Seek(-FooterSize, SeekOrigin::End);
		DEATH_ASSERT(footerPosition >= 0, ".pak file must be opened from seekable stream", );

		std::uint8_t signature[sizeof(Signature)];
		s->Read(signature, sizeof(signature));
		
		// If signature doesn't match, try to find .pak embedded in PE/ELF executable
		if DEATH_UNLIKELY(std::memcmp(signature, Signature, sizeof(Signature)) != 0) {
			footerPosition = FindRelocatedFooter(s);
			DEATH_ASSERT(footerPosition >= 0, "Invalid .pak file", );
		}

		std::uint16_t fileVersion = s->ReadValueAsLE<std::uint16_t>();
		DEATH_ASSERT(fileVersion == Version, "Unsupported .pak file version", );

		PakFileFlags fileFlags = (PakFileFlags)s->ReadValueAsLE<std::uint16_t>();
		std::uint64_t rootIndexOffset = s->ReadValueAsLE<std::uint64_t>();

		bool useRelativeOffsets = (fileFlags & PakFileFlags::RelativeOffsets) == PakFileFlags::RelativeOffsets;
		if (useRelativeOffsets) {
			rootIndexOffset = std::uint64_t(footerPosition + std::int64_t(rootIndexOffset));
		}

		DEATH_ASSERT(rootIndexOffset < std::uint64_t(s->GetSize()), "Invalid root index offset", );
		s->Seek(std::int64_t(rootIndexOffset), SeekOrigin::Begin);

		_path = path;
		_useHashIndex = (fileFlags & PakFileFlags::HashIndex) == PakFileFlags::HashIndex;

		ConstructsItemsFromIndex(*s, nullptr,
			(fileFlags & PakFileFlags::DeflateCompressedIndex) == PakFileFlags::DeflateCompressedIndex,
			useRelativeOffsets, 0);
	}

	StringView PakFile::GetMountPoint() const
	{
		return _mountPoint;
	}

	StringView PakFile::GetPath() const
	{
		return _path;
	}

	bool PakFile::IsValid() const
	{
		return !_path.empty();
	}

	bool PakFile::FileExists(StringView path)
	{
		Item* foundItem = FindItem(path);
		return (foundItem != nullptr && (foundItem->Flags & ItemFlags::Directory) != ItemFlags::Directory);
	}

	bool PakFile::FileExists(std::uint64_t hashedPath)
	{
		DEATH_ASSERT(_useHashIndex, "Hashed path can only be used with hash-indexed .pak files", false);

		Item* foundItem = FindItemByHash(hashedPath);
		return (foundItem != nullptr && (foundItem->Flags & ItemFlags::Directory) != ItemFlags::Directory);
	}

	bool PakFile::DirectoryExists(StringView path)
	{
		Item* foundItem = FindItem(path);
		return (foundItem != nullptr && (foundItem->Flags & ItemFlags::Directory) == ItemFlags::Directory);
	}

	std::unique_ptr<Stream> PakFile::OpenFile(StringView path, std::int32_t bufferSize)
	{
		if DEATH_UNLIKELY(path.empty() || path[path.size() - 1] == '/' || path[path.size() - 1] == '\\') {
			return nullptr;
		}

		Item* foundItem = FindItem(path);
		if DEATH_UNLIKELY(foundItem == nullptr || (foundItem->Flags & ItemFlags::Directory) == ItemFlags::Directory) {
			return nullptr;
		}

		PakPreferredCompression compression = PakPreferredCompression(std::uint32_t(foundItem->Flags & ItemFlags::CompressionFlags) >> CompressionFlagsShift);
		switch (compression) {
			case PakPreferredCompression::None: {
				return std::make_unique<BoundedFileStream>(_path, foundItem->Offset, foundItem->UncompressedSize, bufferSize);
			}
			case PakPreferredCompression::Deflate: {
#if defined(WITH_ZLIB) || defined(WITH_MINIZ)
				return std::make_unique<CompressedBoundedStream<DeflateStream>>(_path, foundItem->Offset, foundItem->UncompressedSize, foundItem->Size, bufferSize);
#else
#	if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("File \"{}\" was compressed with an unsupported method (Deflate)", path);
#	endif
				return nullptr;
#endif
			}
			case PakPreferredCompression::Lz4: {
#if defined(WITH_LZ4)
				return std::make_unique<CompressedBoundedStream<Lz4Stream>>(_path, foundItem->Offset, foundItem->UncompressedSize, foundItem->Size, bufferSize);
#else
#	if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("File \"{}\" was compressed with an unsupported method (LZ4)", path);
#	endif
				return nullptr;
#endif
			}
			case PakPreferredCompression::Zstd: {
#if defined(WITH_ZSTD)
				return std::make_unique<CompressedBoundedStream<ZstdStream>>(_path, foundItem->Offset, foundItem->UncompressedSize, foundItem->Size, bufferSize);
#else
#	if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("File \"{}\" was compressed with an unsupported method (Zstd)", path);
#	endif
				return nullptr;
#endif
			}
			default: {
				DEATH_ASSERT_UNREACHABLE(("File \"{}\" was compressed with an unknown method", path), nullptr);
			}
		}
	}

	std::unique_ptr<Stream> PakFile::OpenFile(std::uint64_t hashedPath, std::int32_t bufferSize)
	{
		DEATH_ASSERT(_useHashIndex, "Hashed path can only be used with hash-indexed .pak files", nullptr);

		Item* foundItem = FindItemByHash(hashedPath);
		if DEATH_UNLIKELY(foundItem == nullptr || (foundItem->Flags & ItemFlags::Directory) == ItemFlags::Directory) {
			return nullptr;
		}

		PakPreferredCompression compression = PakPreferredCompression(std::uint32_t(foundItem->Flags & ItemFlags::CompressionFlags) >> CompressionFlagsShift);
		switch (compression) {
			case PakPreferredCompression::None: {
				return std::make_unique<BoundedFileStream>(_path, foundItem->Offset, foundItem->UncompressedSize, bufferSize);
			}
			case PakPreferredCompression::Deflate: {
#if defined(WITH_ZLIB) || defined(WITH_MINIZ)
				return std::make_unique<CompressedBoundedStream<DeflateStream>>(_path, foundItem->Offset, foundItem->UncompressedSize, foundItem->Size, bufferSize);
#	else
#		if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("File 0x{:.16x} was compressed with an unsupported method (Deflate)", hashedPath);
#		endif
				return nullptr;
#	endif
			}
			case PakPreferredCompression::Lz4: {
#	if defined(WITH_LZ4)
				return std::make_unique<CompressedBoundedStream<Lz4Stream>>(_path, foundItem->Offset, foundItem->UncompressedSize, foundItem->Size, bufferSize);
#	else
#		if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("File 0x{:.16x} was compressed with an unsupported method (LZ4)", hashedPath);
#		endif
				return nullptr;
#	endif
			}
			case PakPreferredCompression::Zstd: {
#	if defined(WITH_ZSTD)
				return std::make_unique<CompressedBoundedStream<ZstdStream>>(_path, foundItem->Offset, foundItem->UncompressedSize, foundItem->Size, bufferSize);
#	else
#		if defined(DEATH_TRACE_VERBOSE_IO)
				LOGE("File 0x{:.16x} was compressed with an unsupported method (Zstd)", hashedPath);
#		endif
				return nullptr;
#	endif
			}
			default: {
				DEATH_ASSERT_UNREACHABLE(("File 0x{:.16x} was compressed with an unknown method", hashedPath), nullptr);
			}
		}
	}

	void PakFile::ConstructsItemsFromIndex(Stream& s, Item* parentItem, bool deflateCompressed, bool useRelativeOffsets, std::uint32_t depth)
	{
		DEATH_ASSERT(depth < MaxDepth, "Maximum directory structure depth reached", );

		std::int64_t indexStartPosition = s.GetPosition();
		
		auto* items = (deflateCompressed
			? ReadIndexFromStreamDeflateCompressed(s, parentItem, useRelativeOffsets, indexStartPosition)
			: ReadIndexFromStream(s, parentItem, useRelativeOffsets, indexStartPosition));

		if DEATH_LIKELY(items != nullptr) {
			for (auto& item : *items) {
				if ((item.Flags & ItemFlags::Directory) == ItemFlags::Directory) {
					s.Seek(std::int64_t(item.Offset), SeekOrigin::Begin);
					ConstructsItemsFromIndex(s, &item, deflateCompressed, useRelativeOffsets, depth + 1);
				}
			}
		}
	}

	Array<PakFile::Item>* PakFile::ReadIndexFromStream(Stream& s, Item* parentItem, bool useRelativeOffsets, std::int64_t indexStartPosition)
	{
		if DEATH_UNLIKELY(parentItem == nullptr) {
			// Root index contains mount point
			std::uint32_t mountPointLength = s.ReadVariableUint32();
			DEATH_ASSERT(mountPointLength < INT16_MAX, "Invalid mount point", nullptr);
			_mountPoint = String{NoInit, mountPointLength};
			s.Read(_mountPoint.data(), std::int32_t(mountPointLength));
			if (!_mountPoint.empty() && _mountPoint[_mountPoint.size() - 1] != '/' && _mountPoint[_mountPoint.size() - 1] != '\\') {
				_mountPoint += FileSystem::PathSeparator;
			}
		}

		std::uint32_t itemCount = s.ReadVariableUint32();

		Array<Item>* items;
		if (parentItem != nullptr) {
			parentItem->ChildItems = Array<Item>(itemCount);
			items = &parentItem->ChildItems;
		} else {
			_rootItems = Array<Item>(itemCount);
			items = &_rootItems;
		}

		for (std::uint32_t i = 0; i < itemCount; i++) {
			Item& item = (*items)[i];

			item.Flags = (ItemFlags)s.ReadVariableUint32();
			bool isDirectory = (item.Flags & PakFile::ItemFlags::Directory) == PakFile::ItemFlags::Directory;

			if (_useHashIndex) {
				DEATH_DEBUG_ASSERT(!isDirectory);
				item.Name = String{NoInit, HashIndexLength};
				s.Read(item.Name.data(), HashIndexLength);
			} else {
				std::uint32_t nameLength = s.ReadVariableUint32();
				DEATH_ASSERT(nameLength == 0 || nameLength < INT32_MAX, "Malformed .pak file", nullptr);
				item.Name = String{NoInit, nameLength};
				s.Read(item.Name.data(), std::int32_t(nameLength));
			}

			if (useRelativeOffsets) {
				// Offset is stored as signed relative offset from the index start position
				std::int64_t relativeOffset = s.ReadVariableInt64();
				item.Offset = std::uint64_t(indexStartPosition + relativeOffset);
			} else {
				item.Offset = s.ReadVariableUint64();
			}

			if (!isDirectory) {
				item.UncompressedSize = s.ReadVariableUint32();
				if (HasCompressedSize(item.Flags)) {
					item.Size = s.ReadVariableUint32();
				}
			}
		}

		return items;
	}

	Array<PakFile::Item>* PakFile::ReadIndexFromStreamDeflateCompressed(Stream& s, Item* parentItem, bool useRelativeOffsets, std::int64_t indexStartPosition)
	{
#if defined(WITH_ZLIB) || defined(WITH_MINIZ)
		DeflateStream ds(s);
		return ReadIndexFromStream(ds, parentItem, useRelativeOffsets, indexStartPosition);
#else
		return nullptr;
#endif
	}

	PakFile::Item* PakFile::FindItem(StringView path)
	{
		path = path.trimmedPrefix("/\\");

		if (_useHashIndex) {
			std::uint64_t hashedPath = FileNameToHash(path);
			return FindItemByHash(hashedPath);
		}

		Array<Item>* items = &_rootItems;
		while (true) {
			auto separator = path.findAnyOr("/\\", path.end());
			auto name = path.prefix(separator.begin());

			if (name.empty()) {
				// Skip all consecutive slashes
				path = path.suffix(separator.end());
				continue;
			}

			// Items are always sorted by PakWriter
			Item* foundItem = std::lower_bound(items->begin(), items->end(), name, [](const PakFile::Item& a, StringView b) {
				return a.Name < b;
			});

			if DEATH_UNLIKELY(foundItem == items->end() || foundItem->Name != name) {
				return nullptr;
			}

			if (separator == path.end()) {
				// If there is no separator left
				return foundItem;
			}

			path = path.suffix(separator.end());
			items = &foundItem->ChildItems;
		}
	}

	PakFile::Item* PakFile::FindItemByHash(std::uint64_t hashedPath)
	{
		// Items are always sorted by PakWriter
		Item* foundItem = std::lower_bound(_rootItems.begin(), _rootItems.end(), hashedPath, [](const PakFile::Item& a, std::uint64_t b) {
			return std::memcmp(a.Name.data(), &b, HashIndexLength) < 0;
		});

		if DEATH_UNLIKELY(foundItem == _rootItems.end() || std::memcmp(foundItem->Name.data(), &hashedPath, HashIndexLength) != 0) {
			return nullptr;
		}

		return foundItem;
	}

	bool PakFile::HasCompressedSize(ItemFlags itemFlags)
	{
		return ((itemFlags & PakFile::ItemFlags::CompressionFlags) != PakFile::ItemFlags::None);
	}

	class PakFile::Directory::Impl
	{
		friend class Directory;

	public:
		Impl(PakFile& pakFile, const StringView path, FileSystem::EnumerationOptions options)
			: _fileNamePart(nullptr)
		{
			Open(pakFile, path, options);
		}

		Impl(const Impl&) = delete;
		Impl& operator=(const Impl&) = delete;

		bool Open(PakFile& pakFile, const StringView path, FileSystem::EnumerationOptions options)
		{
			_options = options;
			_index = 0;

			if (pakFile._useHashIndex) {
				// Cannot enumerate directories when using hash index
				_path[0] = '\0';
				return false;
			}

			if (path.empty()) {
				_childItems = pakFile._rootItems;
			} else {
				Item* parentItem = pakFile.FindItem(path);
				if (parentItem == nullptr) {
					_path[0] = '\0';
					return false;
				}

				_childItems = parentItem->ChildItems;
			}

			if (!_childItems.empty()) {
				std::size_t pathLength = path.size();
				if (pathLength > 0) {
					std::memcpy(_path, path.data(), pathLength);
					if (_path[pathLength - 1] == '/' || _path[pathLength - 1] == '\\') {
#if defined(DEATH_TARGET_WINDOWS)
						_path[pathLength - 1] = '\\';
#else
						_path[pathLength - 1] = '/';
#endif
						_path[pathLength] = '\0';
						_fileNamePart = _path + pathLength;
					} else {
#if defined(DEATH_TARGET_WINDOWS)
						_path[pathLength] = '\\';
#else
						_path[pathLength] = '/';
#endif
						_path[pathLength + 1] = '\0';
						_fileNamePart = _path + pathLength + 1;
					}
				} else {
					_path[0] = '\0';
					_fileNamePart = _path;
				}

				Increment();
				return true;
			} else {
				_path[0] = '\0';
				return false;
			}
		}

		void Increment()
		{
			while (true) {
				if DEATH_UNLIKELY(_index >= _childItems.size()) {
					_path[0] = '\0';
					return;
				}

				Item& item = _childItems[_index];
				if (((_options & FileSystem::EnumerationOptions::SkipDirectories) == FileSystem::EnumerationOptions::SkipDirectories && (item.Flags & ItemFlags::Directory) == ItemFlags::Directory) ||
					((_options & FileSystem::EnumerationOptions::SkipFiles) == FileSystem::EnumerationOptions::SkipFiles && (item.Flags & ItemFlags::Directory) != ItemFlags::Directory)) {
					// Skip this file
					_index++;
					continue;
				}
			
				break;
			}

			auto& fileName = _childItems[_index].Name;
#if defined(DEATH_TARGET_WINDOWS)
			strncpy_s(_fileNamePart, sizeof(_path) - (_fileNamePart - _path), fileName.data(), fileName.size());
#else
			strncpy(_fileNamePart, fileName.data(), std::min(sizeof(_path) - (_fileNamePart - _path), fileName.size()) - 1);
			_path[sizeof(_path) - 1] = '\0';
#endif
			_index++;
		}

	private:

		FileSystem::EnumerationOptions _options;
		char _path[FileSystem::MaxPathLength];
		char* _fileNamePart;
		ArrayView<Item> _childItems;
		std::size_t _index;
	};

	PakFile::Directory::Directory() noexcept
	{
	}

	PakFile::Directory::Directory(PakFile& pakFile, StringView path, FileSystem::EnumerationOptions options)
		: _impl(std::make_shared<Impl>(pakFile, path, options))
	{
	}

	PakFile::Directory::~Directory()
	{
	}

	PakFile::Directory::Directory(const Directory& other)
		: _impl(other._impl)
	{
	}

	PakFile::Directory::Directory(Directory&& other) noexcept
		: _impl(Death::move(other._impl))
	{
	}

	PakFile::Directory& PakFile::Directory::operator=(const Directory& other)
	{
		_impl = other._impl;
		return *this;
	}

	PakFile::Directory& PakFile::Directory::operator=(Directory&& other) noexcept
	{
		_impl = Death::move(other._impl);
		return *this;
	}

	StringView PakFile::Directory::operator*() const& noexcept
	{
		return _impl->_path;
	}

	PakFile::Directory& PakFile::Directory::operator++()
	{
		_impl->Increment();
		return *this;
	}

	bool PakFile::Directory::operator==(const Directory& other) const
	{
		bool isEnd1 = (_impl == nullptr || _impl->_path[0] == '\0');
		bool isEnd2 = (other._impl == nullptr || other._impl->_path[0] == '\0');
		if (isEnd1 || isEnd2) {
			return (isEnd1 && isEnd2);
		}
		return (_impl == other._impl);
	}

	bool PakFile::Directory::operator!=(const Directory& other) const
	{
		return !(*this == other);
	}

	PakFile::Directory::Proxy::Proxy(StringView path)
		: _path(path)
	{
	}

	StringView PakFile::Directory::Proxy::operator*() const& noexcept
	{
		return _path;
	}

	PakWriter::PakWriter(StringView path, bool useHashIndex, bool useCompressedIndex, bool useRelativeOffsets, bool append)
		: _finalized(false), _useHashIndex(useHashIndex), _useCompressedIndex(useCompressedIndex),
			_useRelativeOffsets(useRelativeOffsets)
	{
		_alreadyExisted = FileSystem::FileExists(path);
		if (append) {
			_outputStream = std::make_unique<FileStream>(path, FileAccess::ReadWrite);

			if (_outputStream->GetSize() >= FooterSize) {
				bool isSeekable = _outputStream->Seek(-FooterSize, SeekOrigin::End) >= 0;
				DEATH_ASSERT(isSeekable, ".pak file must be opened from seekable stream", );

				std::uint8_t signature[sizeof(Signature)];
				_outputStream->Read(signature, sizeof(signature));
				if (std::memcmp(signature, Signature, sizeof(Signature)) == 0) {
					LOGE("Appending to existing .pak file \"{}\" is not supported", path);
					_outputStream = nullptr;
					return;
				}
			}

			_outputStream->Seek(0, SeekOrigin::End);
		} else {
			_outputStream = std::make_unique<FileStream>(path, FileAccess::Write);
			_outputStream->Write(OptionalHeader, sizeof(OptionalHeader));
		}
	}

	PakWriter::~PakWriter()
	{
		Finalize();
	}

	bool PakWriter::IsValid() const
	{
		return _outputStream->IsValid();
	}

	bool PakWriter::AddFile(Stream& stream, StringView path, PakPreferredCompression preferredCompression)
	{
		DEATH_ASSERT(_outputStream->IsValid(), "Invalid output stream specified", false);
		DEATH_ASSERT(!path.empty() && path[path.size() - 1] != '/' && path[path.size() - 1] != '\\',
			("\"{}\" is not valid file path", String::nullTerminatedView(path).data()), false);

		Array<PakFile::Item>* items = &_rootItems;
		if (!_useHashIndex) {
			PakFile::Item* parentItem = FindOrCreateParentItem(path);
			if (parentItem != nullptr) {
				items = &parentItem->ChildItems;
			}
		}

		for (PakFile::Item& item : *items) {
			if (item.Name == path) {
				// File already exists in the .pak file
				LOGW("File \"{}\" already exists in the .pak file", path);
				return false;
			}
		}

		PakFile::ItemFlags flags = PakFile::ItemFlags::None;
		std::int64_t offset = _outputStream->GetPosition();
		std::int64_t uncompressedSize, size;

		switch (preferredCompression) {
#if defined(WITH_ZLIB) || defined(WITH_MINIZ)
			case PakPreferredCompression::Deflate: {
				CopyToDeflate(stream, *_outputStream, uncompressedSize);
				size = _outputStream->GetPosition() - offset;
				DEATH_DEBUG_ASSERT(size > 0);
				flags |= PakFile::ItemFlags(std::uint32_t(PakPreferredCompression::Deflate) << PakFile::CompressionFlagsShift);
				break;
			}
#endif
#if defined(WITH_LZ4)
			case PakPreferredCompression::Lz4: {
				CopyToLz4(stream, *_outputStream, uncompressedSize);
				size = _outputStream->GetPosition() - offset;
				DEATH_DEBUG_ASSERT(size > 0);
				flags |= PakFile::ItemFlags(std::uint32_t(PakPreferredCompression::Lz4) << PakFile::CompressionFlagsShift);
				break;
			}
#endif
#if defined(WITH_ZSTD)
			case PakPreferredCompression::Zstd: {
				CopyToZstd(stream, *_outputStream, uncompressedSize);
				size = _outputStream->GetPosition() - offset;
				DEATH_DEBUG_ASSERT(size > 0);
				flags |= PakFile::ItemFlags(std::uint32_t(PakPreferredCompression::Zstd) << PakFile::CompressionFlagsShift);
				break;
			}
#endif
			default: {
				uncompressedSize = stream.CopyTo(*_outputStream);
				size = 0;
				break;
			}
		}

		DEATH_ASSERT(uncompressedSize > 0, "Failed to copy stream to .pak file", false);
		// NOTE: Files inside .pak are limited to 4 GB only for now
		DEATH_ASSERT(uncompressedSize < UINT32_MAX && size < UINT32_MAX, "File size in .pak file exceeded the allowed range", false);

		PakFile::Item* newItem = &arrayAppend(*items, PakFile::Item());
		if (_useHashIndex) {
			newItem->Name = String{NoInit, HashIndexLength};
			std::uint64_t hash = FileNameToHash(path);
			std::memcpy(newItem->Name.data(), &hash, HashIndexLength);
		} else {
			newItem->Name = path;
		}
		newItem->Flags = flags;
		newItem->Offset = offset;
		newItem->UncompressedSize = std::uint32_t(uncompressedSize);
		newItem->Size = std::uint32_t(size);

		return true;
	}

	void PakWriter::Finalize()
	{
		if (_finalized) {
			return;
		}

		_finalized = true;

		Array<PakFile::Item*> queuedDirectories;
		bool hasFiles = false;

		for (PakFile::Item& item : _rootItems) {
			if ((item.Flags & PakFile::ItemFlags::Directory) == PakFile::ItemFlags::Directory) {
				arrayAppend(queuedDirectories, &item);
			} else {
				hasFiles = true;
			}
		}

		for (std::int32_t i = 0; i < queuedDirectories.size(); i++) {
			PakFile::Item& item = *queuedDirectories[i];
			for (PakFile::Item& child : item.ChildItems) {
				if ((child.Flags & PakFile::ItemFlags::Directory) == PakFile::ItemFlags::Directory) {
					arrayAppend(queuedDirectories, &child);
				} else {
					hasFiles = true;
				}
			}
		}

		if DEATH_UNLIKELY(!hasFiles) {
			// No files added - close the stream and try to delete the file
			String path = _outputStream->GetPath();
			_outputStream = nullptr;
			if (!_alreadyExisted) {
				FileSystem::RemoveFile(path);
			}
			return;
		}

		if (!queuedDirectories.empty()) {
			std::size_t i = queuedDirectories.size() - 1;
			while (true) {
				PakFile::Item& item = *queuedDirectories[i];
				item.Offset = _outputStream->GetPosition();

				// Names need to be sorted, because binary search is used to find files
				std::sort(item.ChildItems.begin(), item.ChildItems.end(), [](const PakFile::Item& a, const PakFile::Item& b) {
					return a.Name < b.Name;
				});

#if defined(WITH_ZLIB) || defined(WITH_MINIZ)
				if (_useCompressedIndex) {
					DeflateWriter dw(*_outputStream);
					dw.WriteVariableUint32(std::uint32_t(item.ChildItems.size()));
					for (PakFile::Item& childItem : item.ChildItems) {
						WriteItemDescription(dw, childItem, item.Offset);
					}
				} else
#endif
				{
					_outputStream->WriteVariableUint32(std::uint32_t(item.ChildItems.size()));
					for (PakFile::Item& childItem : item.ChildItems) {
						WriteItemDescription(*_outputStream, childItem, item.Offset);
					}
				}

				if DEATH_UNLIKELY(i == 0) {
					break;
				}
				i--;
			}
		}

		std::int64_t rootIndexOffset = _outputStream->GetPosition();

		// Root index
		{
			// Names need to be sorted, because binary search is used to find files
			std::sort(_rootItems.begin(), _rootItems.end(), [](const PakFile::Item& a, const PakFile::Item& b) {
				return a.Name < b.Name;
			});

			DEATH_ASSERT(_mountPoint.size() < INT16_MAX, "Invalid mount point");

#if defined(WITH_ZLIB) || defined(WITH_MINIZ)
			if (_useCompressedIndex) {
				DeflateWriter dw(*_outputStream);
				dw.WriteVariableUint32(std::uint32_t(_mountPoint.size()));
				dw.Write(_mountPoint.data(), std::int32_t(_mountPoint.size()));

				dw.WriteVariableUint32(std::uint32_t(_rootItems.size()));
				for (PakFile::Item& item : _rootItems) {
					WriteItemDescription(dw, item, rootIndexOffset);
				}
			} else
#endif
			{
				_outputStream->WriteVariableUint32(std::uint32_t(_mountPoint.size()));
				_outputStream->Write(_mountPoint.data(), std::int32_t(_mountPoint.size()));

				_outputStream->WriteVariableUint32(std::uint32_t(_rootItems.size()));
				for (PakFile::Item& item : _rootItems) {
					WriteItemDescription(*_outputStream, item, rootIndexOffset);
				}
			}
		}

		// Footer
		PakFileFlags fileFlags = PakFileFlags::None;
		if (_useHashIndex) {
			fileFlags |= PakFileFlags::HashIndex;
		}
		if (_useRelativeOffsets) {
			fileFlags |= PakFileFlags::RelativeOffsets;
		}
#if defined(WITH_ZLIB) || defined(WITH_MINIZ)
		if (_useCompressedIndex) {
			fileFlags |= PakFileFlags::DeflateCompressedIndex;
		}
#endif

		std::int64_t footerPosition = _outputStream->GetPosition();

		_outputStream->Write(Signature, sizeof(Signature));
		_outputStream->WriteValueAsLE<std::uint16_t>(Version);
		_outputStream->WriteValueAsLE<std::uint16_t>(std::uint16_t(fileFlags));
		
		if (_useRelativeOffsets) {
			// Store root index offset as signed relative offset from footer position
			std::int64_t relativeRootIndexOffset = rootIndexOffset - footerPosition;
			_outputStream->WriteValueAsLE<std::int64_t>(relativeRootIndexOffset);
		} else {
			_outputStream->WriteValueAsLE<std::uint64_t>(rootIndexOffset);
		}

		// Close the stream
		_outputStream = nullptr;
	}

	Containers::StringView PakWriter::GetMountPoint() const
	{
		return _mountPoint;
	}

	void PakWriter::SetMountPoint(Containers::String value)
	{
		_mountPoint = Death::move(value);
	}
	
	PakFile::Item* PakWriter::FindOrCreateParentItem(StringView& path)
	{
		path = path.trimmedPrefix("/\\"_s);

		Array<PakFile::Item>* items = &_rootItems;
		PakFile::Item* parentItem = nullptr;
		while (true) {
			auto separator = path.findAny("/\\"_s);
			if (separator == nullptr) {
				return parentItem;
			}

			auto name = path.prefix(separator.begin());

			PakFile::Item* foundItem = nullptr;
			for (PakFile::Item& item : *items) {
				if (item.Name == name) {
					foundItem = &item;
					break;
				}
			}

			if (foundItem == nullptr) {
				foundItem = &arrayAppend(*items, PakFile::Item());
				foundItem->Name = name;
				foundItem->Flags = PakFile::ItemFlags::Directory;
			}

			path = path.suffix(separator.end());
			parentItem = foundItem;
			items = &foundItem->ChildItems;
		}
	}

	void PakWriter::WriteItemDescription(Stream& s, PakFile::Item& item, std::int64_t indexStartPosition)
	{
		bool isDirectory = (item.Flags & PakFile::ItemFlags::Directory) == PakFile::ItemFlags::Directory;

		s.WriteVariableUint32(std::uint32_t(item.Flags));

		if (_useHashIndex) {
			DEATH_DEBUG_ASSERT(!isDirectory);
			DEATH_DEBUG_ASSERT(item.Name.size() == HashIndexLength);
			s.Write(item.Name.data(), HashIndexLength);
		} else {
			s.WriteVariableUint32(std::uint32_t(item.Name.size()));
			s.Write(item.Name.data(), std::int32_t(item.Name.size()));
		}

		if (_useRelativeOffsets) {
			// Store offset as signed relative offset from the index start position
			std::int64_t relativeOffset = static_cast<std::int64_t>(item.Offset) - indexStartPosition;
			s.WriteVariableInt64(relativeOffset);
		} else {
			s.WriteVariableUint64(item.Offset);
		}

		if (!isDirectory) {
			s.WriteVariableUint32(item.UncompressedSize);

			if (PakFile::HasCompressedSize(item.Flags)) {
				s.WriteVariableUint32(item.Size);
			}
		}
	}

}}