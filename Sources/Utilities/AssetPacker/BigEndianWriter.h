#pragma once

#include "../../Main.h"

#include <cstring>

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace Jazz2::AssetPacker
{
	/**
		@brief Builds a big-endian binary file in memory

		libdragon's formats are big-endian, like the console, and they are written the way libdragon's own tools
		write them: fields whose values are only known later are reserved and filled in afterwards, which is why
		this is a buffer rather than a stream. Offsets are relative to the beginning of the buffer.
	*/
	class BigEndianWriter
	{
	public:
		SmallVector<std::uint8_t, 0> Data;

		std::size_t GetPosition() const {
			return Data.size();
		}

		void Write8(std::uint8_t value) {
			Data.push_back(value);
		}

		void Write16(std::uint16_t value) {
			Write8(std::uint8_t(value >> 8));
			Write8(std::uint8_t(value));
		}

		void Write32(std::uint32_t value) {
			Write16(std::uint16_t(value >> 16));
			Write16(std::uint16_t(value));
		}

		void Write64(std::uint64_t value) {
			Write32(std::uint32_t(value >> 32));
			Write32(std::uint32_t(value));
		}

		void WriteFloat32(float value) {
			std::uint32_t bits;
			std::memcpy(&bits, &value, sizeof(bits));
			Write32(bits);
		}

		void Write(const void* data, std::size_t size) {
			const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
			Data.append(bytes, bytes + size);
		}

		void WriteZeros(std::size_t size) {
			Data.resize(Data.size() + size, 0);
		}

		/** @brief Unsigned LEB128, as libdragon's asset headers store their sizes */
		void WriteLeb128(std::uint64_t value) {
			while (value >= 0x80) {
				Write8(std::uint8_t((value & 0x7F) | 0x80));
				value >>= 7;
			}
			Write8(std::uint8_t(value));
		}

		/** @brief Pads with zeros to the next multiple of @p alignment */
		void Align(std::size_t alignment) {
			while (Data.size() % alignment != 0) {
				Write8(0);
			}
		}

		/** @brief Writes a zero to fill in later, returning where it is */
		std::size_t Reserve16() {
			std::size_t position = Data.size();
			Write16(0);
			return position;
		}

		std::size_t Reserve32() {
			std::size_t position = Data.size();
			Write32(0);
			return position;
		}

		void Patch16(std::size_t position, std::uint16_t value) {
			Data[position] = std::uint8_t(value >> 8);
			Data[position + 1] = std::uint8_t(value);
		}

		void Patch32(std::size_t position, std::uint32_t value) {
			Patch16(position, std::uint16_t(value >> 16));
			Patch16(position + 2, std::uint16_t(value));
		}
	};
}
