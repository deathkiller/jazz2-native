#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <Containers/StringView.h>

#include <cstdint>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::StringView;
#endif

	class EditorSession;

	/**
		@brief Shows a window to save the tiles changed in this level into their tile set

		The tiles changed only in this level (see @ref ShowTileEditor()) become tiles of a tile set: of a new one the
		level then uses, or of the one they're from, which every level that uses it then shows. The level doesn't
		change them anymore afterwards. A tile set of the game isn't written over, a copy of it in the user content
		takes its place. @p source is the tile set chosen first, as @ref TileRef::Source, or @cpp -1 @ce for the
		first one with changed tiles.
	*/
	void ShowApplyTileChangesDialog(EditorSession& session, std::int32_t source = -1);

	/**
		@brief Saves the tiles of a tile set changed in this level into a tile set, as an undoable step

		@p source is the tile set as @ref TileRef::Source. With an empty @p newName, they're saved into the tile set
		itself, otherwise into a new tile set of that name, which the level then uses instead. With @p withPalette, the
		palette of the level becomes the palette of the default tile set, and the level doesn't have its own then.
		Returns `false` if nothing was saved. Undo puts the changes back into the level, but doesn't change the saved file back.
	*/
	bool ApplyTileChanges(EditorSession& session, std::uint8_t source, StringView newName, bool withPalette = false);

	/** @brief Returns how many tiles of a tile set, as @ref TileRef::Source, the level changes */
	std::int32_t CountChangedTiles(EditorSession& session, std::uint8_t source);
}

#endif
