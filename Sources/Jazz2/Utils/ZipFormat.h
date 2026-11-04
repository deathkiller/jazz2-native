#pragma once

#if defined(WITH_EDITOR) || defined(WITH_LEVEL_PACKS) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Main.h"

#include <Containers/DateTime.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

namespace Jazz2::Utils
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::DateTime;
	using Death::Containers::String;
	using Death::Containers::StringView;
#endif

	/**
		@brief Constants and conversions of the ZIP format shared by @ref ZipArchive and @ref ZipWriter
	*/
	class ZipFormat
	{
	public:
		ZipFormat() = delete;

		/** @brief Signature of a local file header */
		static constexpr std::uint32_t LocalFileHeaderSignature = 0x04034B50;
		/** @brief Signature of a central directory file header */
		static constexpr std::uint32_t CentralDirectorySignature = 0x02014B50;
		/** @brief Signature of the end of central directory record */
		static constexpr std::uint32_t EndOfCentralDirectorySignature = 0x06054B50;
		/** @brief Signature of the ZIP64 end of central directory record */
		static constexpr std::uint32_t Zip64EndOfCentralDirectorySignature = 0x06064B50;
		/** @brief Signature of the ZIP64 end of central directory locator */
		static constexpr std::uint32_t Zip64LocatorSignature = 0x07064B50;

		/** @brief Fixed size of a local file header */
		static constexpr std::uint32_t LocalFileHeaderSize = 30;
		/** @brief Fixed size of a central directory file header */
		static constexpr std::uint32_t CentralDirectoryHeaderSize = 46;
		/** @brief Fixed size of the end of central directory record */
		static constexpr std::uint32_t EndOfCentralDirectorySize = 22;

		/** @brief General purpose flag of an encrypted entry */
		static constexpr std::uint16_t EncryptedFlag = 0x0001;
		/** @brief General purpose flag of an entry with UTF-8 name */
		static constexpr std::uint16_t Utf8NameFlag = 0x0800;

		/** @brief Compression method of stored entries */
		static constexpr std::uint16_t MethodStored = 0;
		/** @brief Compression method of entries compressed by Deflate */
		static constexpr std::uint16_t MethodDeflated = 8;

		/** @brief Converts a time to the DOS format of local time used by ZIP */
		static void ToDosDateTime(DateTime time, std::uint16_t& dosTime, std::uint16_t& dosDate);
		/** @brief Converts a name stored in code page 437 to UTF-8 */
		static String DecodeCp437(StringView name);
		/** @brief Continues a CRC-32 checksum with more data, starts with @cpp 0 @ce */
		static std::uint32_t UpdateCrc32(std::uint32_t crc, const void* data, std::size_t length);
	};
}

#endif
