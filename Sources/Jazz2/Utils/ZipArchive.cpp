#include "ZipArchive.h"

#if defined(WITH_EDITOR) || defined(WITH_LEVEL_PACKS)

#include "ZipFormat.h"

#include <Containers/StringConcatenable.h>
#include <IO/BoundedFileStream.h>
#include <IO/FileSystem.h>
#include <IO/Compression/DeflateStream.h>

#include <memory>

using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::IO;
using namespace Death::IO::Compression;

namespace Jazz2::Utils
{
	namespace
	{
		constexpr std::uint64_t MaxDirectorySize = 64 * 1024 * 1024;
		constexpr std::uint16_t Zip64ExtraId = 0x0001;
		constexpr std::uint16_t UnicodePathExtraId = 0x7075;

		std::uint16_t Get16(const std::uint8_t* p)
		{
			return std::uint16_t(p[0] | (p[1] << 8));
		}

		std::uint32_t Get32(const std::uint8_t* p)
		{
			return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
		}

		std::uint64_t Get64(const std::uint8_t* p)
		{
			return std::uint64_t(Get32(p)) | (std::uint64_t(Get32(p + 4)) << 32);
		}

		bool ReadAt(Stream& s, std::uint64_t offset, void* destination, std::size_t length)
		{
			if (s.Seek(std::int64_t(offset), SeekOrigin::Begin) != std::int64_t(offset)) {
				return false;
			}
			return (s.Read(destination, std::int64_t(length)) == std::int64_t(length));
		}

		// Some tools on Windows store backslashes between directories
		String NormalizeName(String name)
		{
			for (char& c : name) {
				if (c == '\\') {
					c = '/';
				}
			}
			return name;
		}
	}

	bool ZipEntry::IsDirectory() const
	{
		return (!Name.empty() && Name[Name.size() - 1] == '/');
	}

	bool ZipEntry::IsEncrypted() const
	{
		return ((Flags & ZipFormat::EncryptedFlag) != 0);
	}

	ZipArchive::ZipArchive()
		: _fileSize(0), _directoryOffset(0)
	{
	}

	ZipError ZipArchive::Open(StringView path, std::uint32_t maxEntries)
	{
		_path = path;
		_entries.clear();
		_fileSize = 0;
		_directoryOffset = 0;

		auto s = fs::Open(path, FileAccess::Read);
		if (!s->IsValid()) {
			return ZipError::CannotOpen;
		}
		std::int64_t fileSize = s->GetSize();
		if (fileSize < std::int64_t(ZipFormat::EndOfCentralDirectorySize)) {
			return ZipError::NotArchive;
		}
		_fileSize = std::uint64_t(fileSize);

		// The end record is last, followed only by a comment of up to 64 kB
		std::uint64_t tailSize = std::min<std::uint64_t>(_fileSize, ZipFormat::EndOfCentralDirectorySize + UINT16_MAX);
		std::uint64_t tailOffset = _fileSize - tailSize;
		std::unique_ptr<std::uint8_t[]> tail = std::make_unique<std::uint8_t[]>(std::size_t(tailSize));
		if (!ReadAt(*s, tailOffset, tail.get(), std::size_t(tailSize))) {
			return ZipError::CannotOpen;
		}

		std::int64_t endPos = -1;
		for (std::int64_t i = std::int64_t(tailSize) - ZipFormat::EndOfCentralDirectorySize; i >= 0; i--) {
			const std::uint8_t* p = tail.get() + i;
			if (Get32(p) == ZipFormat::EndOfCentralDirectorySignature &&
				std::uint64_t(i) + ZipFormat::EndOfCentralDirectorySize + Get16(p + 20) <= tailSize) {
				endPos = i;
				break;
			}
		}
		if (endPos < 0) {
			return ZipError::NotArchive;
		}

		const std::uint8_t* end = tail.get() + endPos;
		std::uint32_t diskNumber = Get16(end + 4);
		std::uint32_t directoryDisk = Get16(end + 6);
		std::uint64_t entryCount = Get16(end + 10);
		std::uint64_t directorySize = Get32(end + 12);
		std::uint64_t directoryOffset = Get32(end + 16);
		std::uint64_t endOffset = tailOffset + std::uint64_t(endPos);

		if (entryCount == UINT16_MAX || directorySize == UINT32_MAX || directoryOffset == UINT32_MAX) {
			if (endOffset < 20) {
				return ZipError::Corrupted;
			}
			std::uint8_t locator[20];
			if (!ReadAt(*s, endOffset - 20, locator, sizeof(locator)) || Get32(locator) != ZipFormat::Zip64LocatorSignature) {
				return ZipError::Corrupted;
			}
			std::uint64_t zip64EndOffset = Get64(locator + 8);
			std::uint8_t zip64End[56];
			if (zip64EndOffset >= endOffset || !ReadAt(*s, zip64EndOffset, zip64End, sizeof(zip64End)) ||
				Get32(zip64End) != ZipFormat::Zip64EndOfCentralDirectorySignature) {
				return ZipError::Corrupted;
			}
			diskNumber = Get32(zip64End + 16);
			directoryDisk = Get32(zip64End + 20);
			entryCount = Get64(zip64End + 32);
			directorySize = Get64(zip64End + 40);
			directoryOffset = Get64(zip64End + 48);
			endOffset = zip64EndOffset;
		}

		if (diskNumber != 0 || directoryDisk != 0) {
			return ZipError::Multipart;
		}
		if (entryCount > maxEntries || directorySize > MaxDirectorySize) {
			return ZipError::TooLarge;
		}
		if (directoryOffset > endOffset || directorySize > endOffset - directoryOffset) {
			return ZipError::Corrupted;
		}

		std::unique_ptr<std::uint8_t[]> directory = std::make_unique<std::uint8_t[]>(std::size_t(directorySize) + 1);
		if (directorySize > 0 && !ReadAt(*s, directoryOffset, directory.get(), std::size_t(directorySize))) {
			return ZipError::Corrupted;
		}

		std::size_t pos = 0;
		_entries.reserve(std::size_t(entryCount));
		for (std::uint64_t i = 0; i < entryCount; i++) {
			if (directorySize - pos < ZipFormat::CentralDirectoryHeaderSize) {
				return ZipError::Corrupted;
			}
			const std::uint8_t* h = directory.get() + pos;
			if (Get32(h) != ZipFormat::CentralDirectorySignature) {
				return ZipError::Corrupted;
			}

			std::uint16_t nameLength = Get16(h + 28);
			std::uint16_t extraLength = Get16(h + 30);
			std::uint16_t commentLength = Get16(h + 32);
			std::size_t recordSize = ZipFormat::CentralDirectoryHeaderSize + std::size_t(nameLength) + extraLength + commentLength;
			if (directorySize - pos < recordSize) {
				return ZipError::Corrupted;
			}

			ZipEntry& entry = _entries.emplace_back();
			entry.Flags = Get16(h + 8);
			entry.Method = Get16(h + 10);
			entry.Crc = Get32(h + 16);
			entry.CompressedSize = Get32(h + 20);
			entry.Size = Get32(h + 24);
			std::uint32_t entryDisk = Get16(h + 34);
			entry.LocalHeaderOffset = Get32(h + 42);

			StringView rawName(reinterpret_cast<const char*>(h + ZipFormat::CentralDirectoryHeaderSize), nameLength);
			if (rawName.find('\0') != nullptr) {
				return ZipError::Corrupted;
			}

			String unicodeName;
			const std::uint8_t* extra = h + ZipFormat::CentralDirectoryHeaderSize + nameLength;
			std::size_t extraPos = 0;
			while (extraLength - extraPos >= 4) {
				std::uint16_t id = Get16(extra + extraPos);
				std::uint16_t size = Get16(extra + extraPos + 2);
				if (extraLength - extraPos - 4 < size) {
					break;
				}
				const std::uint8_t* data = extra + extraPos + 4;
				if (id == Zip64ExtraId) {
					std::size_t dataPos = 0;
					auto takeValue = [&](std::uint64_t& value) {
						if (dataPos + 8 <= size) {
							value = Get64(data + dataPos);
							dataPos += 8;
						}
					};
					if (entry.Size == UINT32_MAX) {
						takeValue(entry.Size);
					}
					if (entry.CompressedSize == UINT32_MAX) {
						takeValue(entry.CompressedSize);
					}
					if (entry.LocalHeaderOffset == UINT32_MAX) {
						takeValue(entry.LocalHeaderOffset);
					}
					if (entryDisk == UINT16_MAX && dataPos + 4 <= size) {
						entryDisk = Get32(data + dataPos);
					}
				} else if (id == UnicodePathExtraId && size > 5 && data[0] == 1 &&
						   Get32(data + 1) == ZipFormat::UpdateCrc32(0, rawName.data(), rawName.size())) {
					unicodeName = String(StringView(reinterpret_cast<const char*>(data + 5), size - 5));
				}
				extraPos += 4 + std::size_t(size);
			}

			if (entryDisk != 0) {
				return ZipError::Multipart;
			}

			if ((entry.Flags & ZipFormat::Utf8NameFlag) != 0) {
				entry.Name = NormalizeName(String(rawName));
			} else if (!unicodeName.empty() && unicodeName.find('\0') == nullptr) {
				entry.Name = NormalizeName(Death::move(unicodeName));
			} else {
				entry.Name = NormalizeName(ZipFormat::DecodeCp437(rawName));
			}

			pos += recordSize;
		}

		_directoryOffset = directoryOffset;
		return ZipError::None;
	}

	ZipError ZipArchive::Extract(const ZipEntry& entry, Stream& target, std::uint64_t maxSize) const
	{
		if (entry.IsEncrypted()) {
			return ZipError::Encrypted;
		}
		if (entry.Method != ZipFormat::MethodStored && entry.Method != ZipFormat::MethodDeflated) {
			return ZipError::UnsupportedMethod;
		}
		if (entry.Size > maxSize || entry.CompressedSize > UINT32_MAX) {
			return ZipError::TooLarge;
		}
		if (entry.Method == ZipFormat::MethodStored && entry.CompressedSize != entry.Size) {
			return ZipError::Corrupted;
		}

		std::uint64_t dataOffset;
		{
			auto s = fs::Open(_path, FileAccess::Read);
			if (!s->IsValid()) {
				return ZipError::CannotOpen;
			}
			std::uint8_t header[ZipFormat::LocalFileHeaderSize];
			if (entry.LocalHeaderOffset >= _directoryOffset || !ReadAt(*s, entry.LocalHeaderOffset, header, sizeof(header)) ||
				Get32(header) != ZipFormat::LocalFileHeaderSignature) {
				return ZipError::Corrupted;
			}
			dataOffset = entry.LocalHeaderOffset + ZipFormat::LocalFileHeaderSize + Get16(header + 26) + Get16(header + 28);
		}
		if (dataOffset > _directoryOffset || entry.CompressedSize > _directoryOffset - dataOffset) {
			return ZipError::Corrupted;
		}

		BoundedFileStream raw(_path, dataOffset, std::uint32_t(entry.CompressedSize));
		if (!raw.IsValid()) {
			return ZipError::CannotOpen;
		}

		std::unique_ptr<DeflateStream> inflater;
		Stream* source = &raw;
		if (entry.Method == ZipFormat::MethodDeflated) {
			std::int32_t inputSize = (entry.CompressedSize <= INT32_MAX ? std::int32_t(entry.CompressedSize) : -1);
			inflater = std::make_unique<DeflateStream>(raw, inputSize, true);
			source = inflater.get();
		}

		std::unique_ptr<std::uint8_t[]> buffer = std::make_unique<std::uint8_t[]>(64 * 1024);
		std::uint64_t written = 0;
		std::uint32_t crc = 0;
		while (written < entry.Size) {
			std::int64_t toRead = std::int64_t(std::min<std::uint64_t>(64 * 1024, entry.Size - written));
			std::int64_t bytesRead = source->Read(buffer.get(), toRead);
			if (bytesRead <= 0) {
				return ZipError::Corrupted;
			}
			if (target.Write(buffer.get(), bytesRead) != bytesRead) {
				return ZipError::CannotWrite;
			}
			crc = ZipFormat::UpdateCrc32(crc, buffer.get(), std::size_t(bytesRead));
			written += std::uint64_t(bytesRead);
		}

		if (crc != entry.Crc) {
			return ZipError::ChecksumMismatch;
		}
		return ZipError::None;
	}

	ZipError ZipArchive::ExtractToFile(const ZipEntry& entry, StringView targetPath, std::uint64_t maxSize) const
	{
		ZipError error;
		{
			auto s = fs::Open(targetPath, FileAccess::Write);
			if (!s->IsValid()) {
				return ZipError::CannotWrite;
			}
			error = Extract(entry, *s, maxSize);
			if (error == ZipError::None && !s->Flush()) {
				error = ZipError::CannotWrite;
			}
		}
		if (error != ZipError::None) {
			fs::RemoveFile(targetPath);
		}
		return error;
	}

	StringView ZipArchive::GetErrorDescription(ZipError error)
	{
		switch (error) {
			case ZipError::None: return "No error"_s;
			case ZipError::CannotOpen: return "The archive cannot be opened"_s;
			case ZipError::NotArchive: return "The file is not a ZIP archive"_s;
			case ZipError::Corrupted: return "The archive is damaged"_s;
			case ZipError::Multipart: return "Archives split into several files are not supported"_s;
			case ZipError::Encrypted: return "Encrypted archives are not supported"_s;
			case ZipError::UnsupportedMethod: return "The archive uses an unsupported compression method"_s;
			case ZipError::TooLarge: return "The archive or a file in it is too large"_s;
			case ZipError::ChecksumMismatch: return "A file in the archive is damaged"_s;
			case ZipError::CannotWrite: return "The file cannot be written"_s;
			default: return "Unknown error"_s;
		}
	}
}

#endif
