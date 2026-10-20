#pragma once

#include "../../Main.h"

#include <Containers/SmallVector.h>

namespace Jazz2::Formats
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::SmallVectorImpl;
#endif

	/**
	 * @brief Appends a variable-length unsigned integer, as the chunks of a level store them
	 *
	 * 7 bits a byte, the lowest ones first, the highest bit of a byte set if another byte follows.
	 */
	inline void WriteVarUint(SmallVectorImpl<std::uint8_t>& data, std::uint32_t value)
	{
		do {
			std::uint8_t byte = std::uint8_t(value & 0x7F);
			value >>= 7;
			data.push_back(value != 0 ? std::uint8_t(byte | 0x80) : byte);
		} while (value != 0);
	}

	/**
	 * @brief Reads a variable-length unsigned integer written by @ref WriteVarUint() and moves past it
	 *
	 * Returns `false` if it doesn't end before @p end, it's longer than 5 bytes, or its value doesn't fit 32 bits,
	 * as @ref LevelReader rejects such a number in the body.
	 */
	inline bool ReadVarUint(const std::uint8_t*& p, const std::uint8_t* end, std::uint32_t& value)
	{
		value = 0;
		for (std::int32_t shift = 0; shift < 35 && p < end; shift += 7) {
			std::uint8_t byte = *p++;
			value |= std::uint32_t(byte & 0x7F) << shift;
			if ((byte & 0x80) == 0) {
				// The fifth byte holds only the four highest bits of the value
				return (shift < 28 || byte <= 0x0F);
			}
		}
		return false;
	}
}
