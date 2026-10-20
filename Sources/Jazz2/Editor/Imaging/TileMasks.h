#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Formats/TileSetFile.h"

#include <cstring>

namespace Jazz2::Editor
{
	/**
		@brief Collision masks of tiles packed a bit per pixel, as tile sets store them

		Row by row, the lowest bit of a byte first (see @ref Formats::TileSetData::Masks). The level stores the masks it
		overrides a byte per pixel instead (see @ref Formats::TileMaskOverride), these convert between the two.
	*/
	namespace TileMasks
	{
		/** @brief Pixels of a tile */
		constexpr std::int32_t PixelCount = Formats::TileSetFile::TileSize * Formats::TileSetFile::TileSize;
		/** @brief Bytes of a packed mask of a tile */
		constexpr std::int32_t BytesPerTile = Formats::TileSetData::MaskBytesPerTile;

		/** @brief Returns whether a pixel of a packed mask is solid */
		inline bool GetBit(const std::uint8_t* bits, std::int32_t index)
		{
			return ((bits[index / 8] >> (index % 8)) & 1) != 0;
		}

		/** @brief Makes a pixel of a packed mask solid */
		inline void SetBit(std::uint8_t* bits, std::int32_t index)
		{
			bits[index / 8] |= std::uint8_t(1 << (index % 8));
		}

		/** @brief Packs a mask of a tile, a byte per pixel (anything but 0 is solid), into @ref BytesPerTile bytes */
		inline void Pack(const std::uint8_t* values, std::uint8_t* bits)
		{
			std::memset(bits, 0, BytesPerTile);
			for (std::int32_t i = 0; i < PixelCount; i++) {
				if (values[i] != 0) {
					SetBit(bits, i);
				}
			}
		}

		/** @brief Unpacks a mask of a tile into a byte per pixel, 1 for solid, 0 otherwise */
		inline void Unpack(const std::uint8_t* bits, std::uint8_t* values)
		{
			for (std::int32_t i = 0; i < PixelCount; i++) {
				values[i] = std::uint8_t((bits[i / 8] >> (i % 8)) & 1);
			}
		}
	}
}

#endif
