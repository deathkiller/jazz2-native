#pragma once

#include "../../Main.h"

#include <memory>

#include <Containers/StringView.h>
#include <IO/Stream.h>

using namespace Death::Containers;
using namespace Death::IO;

namespace Jazz2::Compatibility
{
	/**
		@brief Processes compressed or uncompressed blocks from original files (in little endian)
		
		Buffers a single (optionally zlib-compressed) data block from an original game file and exposes
		sequential little-endian readers for the primitive types used by the format parsers. Used as the
		low-level building block by @ref JJ2Level, @ref JJ2Tileset and related importers.
	*/
	class JJ2Block
	{
	public:
		/** @brief Creates a new instance from the specified stream, decompressing the block if needed */
		JJ2Block(std::unique_ptr<Stream>& s, std::int32_t length, std::int32_t uncompressedLength = 0);

		/** @brief Seeks to the specified offset within the block */
		void SeekTo(std::int32_t offset);
		/** @brief Skips the specified number of bytes */
		void DiscardBytes(std::int32_t length);

		/** @brief Reads a boolean value */
		bool ReadBool();
		/** @brief Reads an unsigned 8-bit integer */
		std::uint8_t ReadByte();
		/** @brief Reads a signed 16-bit integer */
		std::int16_t ReadInt16();
		/** @brief Reads an unsigned 16-bit integer */
		std::uint16_t ReadUInt16();
		/** @brief Reads a signed 32-bit integer */
		std::int32_t ReadInt32();
		/** @brief Reads an unsigned 32-bit integer */
		std::uint32_t ReadUInt32();
		/** @brief Reads a 7-bit encoded integer */
		std::int32_t ReadUint7bitEncoded();
		/** @brief Reads a 32-bit floating-point value */
		float ReadFloat();
		/** @brief Reads a fixed-point value encoded as an integer and returns it as floating-point */
		float ReadFloatEncoded();
		/** @brief Reads the specified number of raw bytes into the destination buffer */
		void ReadRawBytes(std::uint8_t* dst, std::int32_t length);
		/** @brief Reads a string of the specified length, optionally trimming at the first null character */
		StringView ReadString(std::int32_t length, bool trimToNull);

		/** @brief Returns whether the end of the block has been reached */
		bool ReachedEndOfStream() {
			return (_offset == INT32_MAX);
		}

		/** @brief Returns the total length of the block in bytes */
		std::int32_t GetLength() {
			return _length;
		}

	private:
		std::unique_ptr<std::uint8_t[]> _buffer;
		std::int32_t _length;
		std::int32_t _offset;
	};
}