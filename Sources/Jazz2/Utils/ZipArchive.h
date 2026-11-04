#pragma once

#if defined(WITH_EDITOR) || defined(WITH_LEVEL_PACKS) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Main.h"

#include <Containers/ArrayView.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>
#include <IO/Stream.h>

namespace Jazz2::Utils
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::ArrayView;
	using Death::Containers::SmallVector;
	using Death::Containers::String;
	using Death::Containers::StringView;
	using Death::IO::Stream;
#endif

	/** @brief Why a ZIP archive or its entry couldn't be read */
	enum class ZipError : std::uint8_t
	{
		/** Nothing failed */
		None,
		/** The file can't be opened */
		CannotOpen,
		/** The file isn't a ZIP archive */
		NotArchive,
		/** The archive is damaged or its records don't agree */
		Corrupted,
		/** The archive is split into several files */
		Multipart,
		/** The entry is encrypted */
		Encrypted,
		/** The entry is compressed by a method other than Deflate */
		UnsupportedMethod,
		/** The entry or the archive is larger than allowed */
		TooLarge,
		/** The content doesn't match its checksum */
		ChecksumMismatch,
		/** The target can't be written */
		CannotWrite
	};

	/** @brief One file or directory in a ZIP archive */
	struct ZipEntry
	{
		/** @brief Name in UTF-8, with `/` between directories, as stored in the archive */
		String Name;
		/** @brief Uncompressed size */
		std::uint64_t Size;
		/** @brief Compressed size */
		std::uint64_t CompressedSize;
		/** @brief Offset of the local file header */
		std::uint64_t LocalHeaderOffset;
		/** @brief CRC-32 of the uncompressed content */
		std::uint32_t Crc;
		/** @brief Compression method */
		std::uint16_t Method;
		/** @brief General purpose flags */
		std::uint16_t Flags;

		/** @brief Whether it's a directory */
		bool IsDirectory() const;
		/** @brief Whether it's encrypted */
		bool IsEncrypted() const;
	};

	/**
		@brief Reads a ZIP archive

		Supports stored and Deflate entries, names in UTF-8 or code page 437, and ZIP64. Only the central directory is
		read by @ref Open(), the entries are decompressed on request and checked against their size and CRC-32, so a
		damaged or forged archive never produces more than it declares.
	*/
	class ZipArchive
	{
	public:
		ZipArchive();

		/**
		 * @brief Opens an archive and reads its directory
		 *
		 * @param path          Archive file
		 * @param maxEntries    Largest number of entries accepted
		 */
		ZipError Open(StringView path, std::uint32_t maxEntries = 65536);

		/** @brief Returns all entries of the archive */
		ArrayView<const ZipEntry> GetEntries() const {
			return _entries;
		}

		/**
		 * @brief Decompresses an entry into a stream
		 *
		 * Fails with @ref ZipError::TooLarge if the entry is larger than @p maxSize, before anything is written.
		 */
		ZipError Extract(const ZipEntry& entry, Stream& target, std::uint64_t maxSize = UINT64_MAX) const;
		/** @brief Decompresses an entry into a file, the file is removed if it fails */
		ZipError ExtractToFile(const ZipEntry& entry, StringView targetPath, std::uint64_t maxSize = UINT64_MAX) const;

		/** @brief Returns a short description of an error for logs and messages */
		static StringView GetErrorDescription(ZipError error);

	private:
		String _path;
		std::uint64_t _fileSize;
		std::uint64_t _directoryOffset;
		SmallVector<ZipEntry, 0> _entries;
	};
}

#endif
