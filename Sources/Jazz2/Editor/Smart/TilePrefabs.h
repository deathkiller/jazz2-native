#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Tools/TileStamp.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>

namespace Jazz2::Editor
{
	using Death::Containers::SmallVector;
	using Death::Containers::String;

	/**
		@brief A stamp of tiles kept with a tile set under a name, to paint it in any level of the tile set

		Tiles of the tile set are kept by their index, tiles of extra tile sets by the name of their tile set, so they're
		found in another level that has the same tile set too. Animated tiles belong to a single level, they're left
		out. Prefabs are kept in the rules of the tile set, `Tilesets/<name>.rules`, a JSON file found as tile sets
		are --- the user content first --- and saved into the user content.
	*/
	struct TilePrefab
	{
		/** @brief Tile of a prefab */
		struct Tile
		{
			/** @brief Whether it's painted at all */
			bool Present = false;
			/** @brief Extra tile set it's from, empty for the tile set of the rules */
			String TileSet;
			/** @brief Index in its tile set */
			std::uint16_t Index = 0;
			/** @brief Flags, see @ref Formats::TileRecord */
			std::uint8_t Flags = 0;
		};

		/** @brief Name */
		String Name;
		/** @brief Width in tiles */
		std::int32_t Width = 0;
		/** @brief Height in tiles */
		std::int32_t Height = 0;
		/** @brief Tiles row by row */
		SmallVector<Tile, 0> Tiles;

		/** @brief Reads the prefabs of a tile set, returns `false` if it has no rules */
		static bool Load(StringView tileSet, SmallVectorImpl<TilePrefab>& prefabs);
		/** @brief Writes the prefabs of a tile set into its rules in the user content, keeping the rest of them */
		static bool Save(StringView tileSet, const SmallVectorImpl<TilePrefab>& prefabs);
		/** @brief Makes a prefab of a stamp of a level */
		static TilePrefab FromStamp(const LevelDocument& document, const TileStamp& stamp, StringView name);
		/** @brief Makes a stamp of a prefab for a level, tiles of tile sets the level doesn't have are left out */
		TileStamp ToStamp(const LevelDocument& document) const;
	};
}

#endif
