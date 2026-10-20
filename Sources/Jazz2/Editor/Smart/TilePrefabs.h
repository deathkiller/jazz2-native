#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Common/StampClipboard.h"
#include "../Common/TileStamp.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>

#include <jsoncpp/forwards.h>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::SmallVector;
	using Death::Containers::String;
#endif

	/**
		@brief A stamp of tiles kept with a tile set under a name, to paint it in any level of the tile set

		Tiles of the tile set are kept by their index, tiles of extra tile sets by the name of their tile set, so they're
		found in another level that has the same tile set too. Animated tiles and tiles of a tile set the level has in
		itself belong to a single level, they're left out. Prefabs are kept in the rules of the tile set, `Tilesets/<name>.rules`, a JSON file found as tile sets
		are --- the user content first --- and saved into the user content.

		A prefab can have tiles of other layers too, by their position from the layer it's used on, and events, by
		their offset from its top left corner (see @ref LayeredStamp), those off the grid in pixels, in `"offgrid"`.
		Older versions read only its tiles, or only the events on the grid.
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

		/** @brief Tiles of another layer */
		struct LayerTiles
		{
			/** @brief Position of the layer in the list of layers from the layer the prefab is used on, never 0 */
			std::int32_t FromLayer = 0;
			/** @brief Tiles row by row */
			SmallVector<Tile, 0> Tiles;
		};
		/** @brief Event of a prefab */
		struct PrefabEvent
		{
			/** @brief Offset from the top left corner of the prefab */
			Vector2i Offset;
			/** @brief The event */
			Formats::EventRecord Event;
		};
		/** @brief Tiles of other layers */
		SmallVector<LayerTiles, 0> ExtraLayers;
		/** @brief Events */
		SmallVector<PrefabEvent, 0> Events;
		/** @brief Events off the grid, @ref PrefabEvent::Offset in pixels from the top left corner of the prefab */
		SmallVector<PrefabEvent, 0> OffGridEvents;
		/**
		 * @brief Tiles of the stamp it was made of that were left out, they're of a tile set the level has in itself
		 *
		 * Only set by @ref FromStamp() and @ref FromLayered(), it isn't kept with the prefab. Such tiles are of a single
		 * level, another level would take its own tiles at their place.
		 */
		std::int32_t DroppedTiles = 0;

		/** @brief Returns `true` if it has tiles of other layers or events, so it's placed as a stamp of several layers */
		bool HasLayers() const {
			return (!ExtraLayers.empty() || !Events.empty() || !OffGridEvents.empty());
		}

		/** @brief Reads the prefabs of a tile set, returns `false` if it has no rules */
		static bool Load(StringView tileSet, SmallVectorImpl<TilePrefab>& prefabs);
		/** @brief Writes the prefabs of a tile set into its rules in the user content, keeping the rest of them, nothing if they can't be read */
		static bool Save(StringView tileSet, const SmallVectorImpl<TilePrefab>& prefabs);
		/** @brief Makes a prefab of a stamp of a level */
		static TilePrefab FromStamp(const LevelDocument& document, const TileStamp& stamp, StringView name);
		/** @brief Makes a stamp of a prefab for a level, tiles of tile sets the level doesn't have are left out */
		TileStamp ToStamp(const LevelDocument& document) const;
		/**
		 * @brief Makes a prefab of a stamp of several layers of a level
		 *
		 * @p mainLayer is the layer the prefab is used on, by its position from the sprite layer, see
		 * @ref LayeredStamp::Layer::FromSpriteLayer.
		 */
		static TilePrefab FromLayered(const LevelDocument& document, const LayeredStamp& stamp, std::int32_t mainLayer, StringView name);
		/** @brief Makes a stamp of several layers of a prefab for a level, used on a layer by its position from the sprite layer */
		LayeredStamp ToLayered(const LevelDocument& document, std::int32_t mainLayer) const;

	private:
		static bool ReadTiles(const Json::Value& tiles, std::int32_t count, SmallVectorImpl<Tile>& result);
		static Json::Value WriteTiles(const SmallVectorImpl<Tile>& tiles);
		// Reads events whose offsets are below the limits, those that aren't are left out
		static void ReadEvents(const Json::Value& events, Vector2i limit, SmallVectorImpl<PrefabEvent>& result);
		static Json::Value WriteEvents(const SmallVectorImpl<PrefabEvent>& events);
		// Returns the number of tiles left out, of a tile set the level has in itself
		static std::int32_t TilesFromStamp(const LevelDocument& document, const TileStamp& stamp, SmallVectorImpl<Tile>& result);
		static TileStamp TilesToStamp(const LevelDocument& document, const SmallVectorImpl<Tile>& tiles, std::int32_t width, std::int32_t height);
	};
}

#endif
