#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "EventStamp.h"
#include "TileStamp.h"

#include <Containers/String.h>
#include <Containers/StringView.h>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::String;
	using Death::Containers::StringView;
#endif

	/**
		@brief A rectangle of tiles of several layers with the events in it, copied and pasted as one
	*/
	struct LayeredStamp
	{
		/** @brief Tiles of a layer */
		struct Layer
		{
			/** @brief Position of the layer in the list of layers, from the sprite layer, so another level with more layers gets them right */
			std::int32_t FromSpriteLayer = 0;
			/** @brief The tiles, as large as the stamp */
			TileStamp Tiles;
		};

		/** @brief Width in tiles */
		std::int32_t Width = 0;
		/** @brief Height in tiles */
		std::int32_t Height = 0;
		/** @brief Where it was copied from, in tiles, where it's pasted if the pointer isn't over the level */
		Vector2i Origin;
		/** @brief Tiles of each layer copied */
		SmallVector<Layer, 0> Layers;
		/** @brief Events in the rectangle, by their offsets from its top left corner */
		EventStamp Events;

		/** @brief Returns `true` if there's nothing to paste */
		bool IsEmpty() const {
			return (Width <= 0 || Height <= 0 || (Layers.empty() && Events.IsEmpty()));
		}
	};

	/**
		@brief Stamps as text in the clipboard of the system, so they can be pasted into another instance of the game

		The text is JSON: `{"J2R":1,"w":...,"h":...,"t":[source,index,flags,...]}` for tiles (source -1 for a tile
		left out), followed by `"events":[...]` and `"offgrid":[...]` as below for tiles with their events (see
		@ref TileStamp::Events, an event not listed is empty), `{"J2R":1,"events":[x,y,type,flags,generator flags,generator delay,params...],"offgrid":[...]}` for
		events, those off the grid by their offsets in pixels in the same form (older versions read only those on it), and
		`{"J2R":2,"w":...,"h":...,"x":...,"y":...,"layers":[{"o":...,"t":[...]},...],"events":[...]}` for several layers
		with their events. Older versions read the first layer of the last one as tiles, and its events as events.
		Everything is read without trusting it, a text that isn't whole or is too large isn't a stamp, and events this
		version doesn't know are left out.

		Tiles written with the level they're of have `"sets":[name,offset,embedded,...]` after them too, the default tile
		set and the extra ones by their sources. Read into another level, its tiles are found in the tile sets of the
		same names, in whatever slots it has them, and tiles of tile sets it hasn't, or of one a level has in itself, are
		left out. A text without them, of an older version, is read as it is.
	*/
	namespace StampClipboard
	{
		/** @brief The longest side of a stamp in tiles, larger ones aren't read, nor picked */
		constexpr std::int32_t MaxStampSide = 65535;
		/** @brief The most tiles a stamp has, larger ones aren't read, nor picked */
		constexpr std::int32_t MaxStampTiles = 1 << 20;

		/** @brief Returns tiles as text, with the tile sets of @p document they're of if it's given */
		String WriteTiles(const TileStamp& stamp, const LevelDocument* document = nullptr);
		/** @brief Reads tiles from text, returns `false` if it isn't a stamp, with @p document they're found in its tile sets */
		bool ReadTiles(StringView text, TileStamp& stamp, const LevelDocument* document = nullptr);
		/** @brief Returns events as text */
		String WriteEvents(const EventStamp& stamp);
		/** @brief Reads events from text, returns `false` if there are none */
		bool ReadEvents(StringView text, EventStamp& stamp);
		/** @brief Returns tiles of several layers and events as text, with the tile sets of @p document as @ref WriteTiles() */
		String WriteLayered(const LayeredStamp& stamp, const LevelDocument* document = nullptr);
		/** @brief Reads tiles of several layers and events from text, returns `false` if it isn't such a stamp, see @ref ReadTiles() */
		bool ReadLayered(StringView text, LayeredStamp& stamp, const LevelDocument* document = nullptr);

		/** @brief Returns whether a level has a tile, a stamp copied from another level can refer to tile sets and animated tiles it hasn't */
		bool HasTile(const LevelDocument& document, const TileRef& tile);
		/** @brief Leaves tiles the level doesn't have out of a stamp, as if they weren't copied */
		void RemoveUnknownTiles(const LevelDocument& document, TileStamp& stamp);
	}
}

#endif
