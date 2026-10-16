#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <Containers/ArrayView.h>
#include <Containers/StringView.h>

#include <cstdint>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::ArrayView;
	using Death::Containers::StringView;
#endif

	class EditorSession;
	class LevelDocument;

	/**
		@brief Shows a window to delete tiles of the default tile set and to put them in another order

		Every level that uses the tile set refers to its tiles by their positions, so the levels are changed with it.
		The tile set is saved either as itself --- a copy in the user content, which every level of the game then
		uses, with the levels found in places that can be written to changed too and the others listed, as they would
		show other tiles --- or as a new tile set, which only this level uses.
	*/
	void ShowTileSetLayoutDialog(EditorSession& session);

	/**
		@brief Changes the tiles of the default tile set of a level to a new order of the tile set

		@p order gives the old position of the tile at each new position, -1 for a new empty tile. Tiles that are
		left out are gone, the level shows the empty tile instead. Returns the number of tiles of the layers that
		were cleared, or -1 if the level doesn't use the tile set.
	*/
	std::int32_t RemapLevelToTileSetOrder(LevelDocument& document, ArrayView<const std::int32_t> order);
}

#endif
