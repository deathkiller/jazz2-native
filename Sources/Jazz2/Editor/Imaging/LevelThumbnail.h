#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Document/LevelDocument.h"
#include "../../Formats/LevelPreview.h"

#include <unordered_map>

namespace Jazz2::Tiles
{
	class TileMap;
}

namespace Jazz2::Editor
{
	class TileSetDataCache;

	/**
		@brief Draws a level small, a pixel for a tile in the average color of the tile

		The colors of the tiles are remembered, so a level is drawn again quickly. Indexed tiles are in the palette
		the level is shown in.
	*/
	class LevelThumbnail
	{
	public:
		/** @brief Pixels of a tile as stored in its tile set */
		using TilePixels = Formats::LevelPreview::TilePixels;

		/** @brief Creates a new instance for a level, the tiles of its tile sets are read whole from @p tileSets */
		LevelThumbnail(const LevelDocument& document, TileSetDataCache& tileSets);

		/** @brief Returns the average color of a tile as `R | G << 8 | B << 16 | A << 24`, its alpha is how much of it is covered */
		std::uint32_t GetTileColor(TileRef tile);
		/** @brief Draws a layer, a pixel for each of its tiles, row by row */
		void DrawLayer(std::int32_t layer, SmallVectorImpl<std::uint32_t>& pixels);
		/** @brief Forgets the colors of the tiles, when the palette or the tile sets change */
		void Clear();
		/**
		 * @brief Forgets the colors of the tiles if what they're made of changed since the last call, returns `true` then
		 *
		 * Compares @ref GetSourceHash() with the one of the last call. Meant to be called whenever the level changed,
		 * before it's drawn again; the colors of the tiles that stay the same are kept.
		 */
		bool Revalidate();
		/**
		 * @brief Returns a hash of everything the colors of the tiles of a level depend on
		 *
		 * That's the palette the level is shown in, the tile sets and the data @p tileSets has for them --- a tile set
		 * read again from its file is new data --- their offsets, flags and remapping, the tiles stored in the level
		 * itself and the first frames of the animations. Hashing the tiles stored in the level takes a moment for a
		 * large set of them, so it's meant for once per change of the level, not for every frame.
		 */
		static std::uint64_t GetSourceHash(const LevelDocument& document, TileSetDataCache& tileSets);

		/**
		 * @brief Makes the preview of a level shown in the list of custom levels, see @ref Formats::LevelFile::PreviewChunk
		 *
		 * What the game shows around the start of the level, see @ref Formats::LevelPreview. The layers are placed as
		 * @p tileMap has them, if it's given, the indexed tiles are in the palette the level is shown in.
		 */
		static void MakePreview(const LevelDocument& document, Tiles::TileMap* tileMap, TileSetDataCache& tileSets, SmallVectorImpl<std::uint8_t>& data);

		/** @brief Finds the pixels of a tile, the first frame of an animated one, returns `false` for an empty or unknown one */
		static bool GetTilePixels(const LevelDocument& document, TileSetDataCache& tileSets, TileRef tile, TilePixels& result);

	private:
		const LevelDocument& _document;
		TileSetDataCache& _tileSets;
		std::unordered_map<std::uint32_t, std::uint32_t> _colors;
		// What the colors were found from, see Revalidate()
		std::uint64_t _sourceHash = 0;

		std::uint32_t ComputeTileColor(TileRef tile);
	};
}

#endif
