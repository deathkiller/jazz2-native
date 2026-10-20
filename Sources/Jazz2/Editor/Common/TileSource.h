#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Document/LevelDocument.h"

namespace Jazz2::Editor
{
	/**
		@brief The tile set a source of tiles of a level takes them from, see @ref ResolveTileSource()

		A tile of the source is the tile @cpp Offset + TileRef::Index @ce of the tile set.
	*/
	struct TileSource
	{
		/** @brief Name of the tile set */
		StringView Name;
		/** @brief The first tile of the source in the tile set */
		std::int32_t Offset = 0;
		/** @brief Record of the extra tile set, `nullptr` for the default tile set */
		const Formats::ExtraTileSetRecord* Extra = nullptr;
		/**
		 * @brief Table that matches the palette indices of the 8-bit tiles of the tile set to the palette of the level
		 *
		 * `nullptr` if they're shown as they are --- the default tile set, a tile set that isn't matched, and one with
		 * true color tiles, which isn't matched to the palette of the level.
		 */
		const std::uint8_t* Remapping = nullptr;

		/** @brief Returns `true` if the tile set is stored in the level itself */
		bool IsEmbedded() const {
			return (Extra != nullptr && (Extra->Flags & Formats::ExtraTileSetRecord::Embedded) != 0);
		}
	};

	/**
		@brief Finds the tile set a source of tiles of a level takes them from

		Returns `false` for animated and raw tiles, which aren't of any tile set, and for an extra tile set the level
		doesn't have.
	*/
	inline bool ResolveTileSource(const LevelDocument& document, std::uint8_t source, TileSource& result)
	{
		result = TileSource();
		if (source == TileRef::DefaultTileSet) {
			result.Name = document.Properties.DefaultTileSet;
			return true;
		}
		if (source == TileRef::Animated || source == TileRef::Raw || std::size_t(source) - 1 >= document.ExtraTileSets.size()) {
			return false;
		}
		const Formats::ExtraTileSetRecord& extra = document.ExtraTileSets[std::size_t(source) - 1];
		result.Name = extra.Name;
		result.Offset = extra.Offset;
		result.Extra = &extra;
		if ((extra.Flags & (Formats::ExtraTileSetRecord::Remapped | Formats::ExtraTileSetRecord::TrueColor)) == Formats::ExtraTileSetRecord::Remapped) {
			result.Remapping = extra.Remapping;
		}
		return true;
	}
}

#endif
