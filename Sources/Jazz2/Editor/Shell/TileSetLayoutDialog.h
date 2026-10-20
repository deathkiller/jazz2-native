#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <Containers/ArrayView.h>
#include <Containers/StringView.h>

#include <cstdint>

namespace Jazz2::Formats
{
	struct TileSetData;
}

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::ArrayView;
	using Death::Containers::StringView;
#endif

	class IEditorContext;
	class LevelDocument;

	/**
		@brief Writes a tile set file that got new tiles at its end, with the other levels that would show other tiles

		A level refers to the tiles of its extra tile sets by IDs that follow those of its default tile set, so every
		level found that has the tile set as its default one and extra tile sets after it is changed with it, its extra
		tiles keep showing what they did. If any of them can't be changed --- it's in the files of the game, or open in
		another editor --- nothing is written and the levels are listed. All files are written next to them first and
		replaced only if every one could be written, the previous ones are kept as `.bak` files. @p oldCount is the
		number of tiles the tile set had before. Returns `false` if nothing was written, a message tells why.
	*/
	bool WriteTileSetWithAddedTiles(IEditorContext& session, StringView tileSet, StringView path, const Formats::TileSetData& data, std::int32_t oldCount);

	/**
		@brief Shows a window to delete tiles of the default tile set and to put them in another order

		Every level that uses the tile set refers to its tiles by their positions, so the levels are changed with it.
		The tile set is saved either as itself --- a copy in the user content, which every level of the game then
		uses, with the levels found changed too, so only if all of them are in places that can be written to and none
		is open in another editor --- or as a new tile set, which only this level uses. Changing the tile set itself
		writes all files first next to them, off the user interface, and replaces them only if every one could be
		written, the previous ones are kept as `.bak` files. Prefabs, terrain rules and slope tags follow the new order
		too, and are replaced with the rest. It can't be undone, the history of the level is cleared then.
	*/
	void ShowTileSetLayoutDialog(IEditorContext& session);

	/**
		@brief Changes the tiles of a tile set in a level to a new order of the tile set

		@p order gives the old position of the tile at each new position, -1 for a new empty tile. Tiles that are
		left out are gone, the level shows the empty tile instead. @p tileSet is the name of the tile set, empty for the
		default tile set of the level. The level follows it as its default tile set and as any extra tile set that
		isn't stored in the level, an extra one then takes the part of the tile set its tiles are in now. @p mergedInto
		gives the old tile each old tile was merged into as its duplicate, -1 for those that weren't, a merged tile is
		then shown as that one. Returns the number of tiles of the layers that were cleared, or -1 if the level doesn't
		use the tile set.
	*/
	std::int32_t RemapLevelToTileSetOrder(LevelDocument& document, ArrayView<const std::int32_t> order, StringView tileSet = {},
		ArrayView<const std::int32_t> mergedInto = {});
}

#endif
