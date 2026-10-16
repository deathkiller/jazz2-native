#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <Containers/StringView.h>

namespace Jazz2::Editor
{
	class EditorSession;

	/**
		@brief Asks for an image and makes a new tile set of it, in the user content

		The image is cut into tiles of 32 × 32 pixels row by row, an image of the masks next to it (`.mask.png`, as
		the editor exports them) gives their masks, otherwise they're solid where they aren't transparent. The new tile
		set can be added to the level right away.
	*/
	void ShowImportTileSetDialog(EditorSession& session);

	/**
	 * @brief Makes a new tile set of an image as @ref ShowImportTileSetDialog() does, for testing
	 *
	 * @p colors is `own`, `true`, `match` or `reduce`, empty keeps the colors as they are.
	 */
	void ImportTileSetImage(EditorSession& session, Death::Containers::StringView path, Death::Containers::StringView name, bool addToLevel, bool asDefault,
		Death::Containers::StringView colors = {}, bool dither = false);

	/**
		@brief Asks for a name and a number of tiles and makes a new tile set of empty tiles, in the user content

		It has the palette of the level. Its tiles are painted as tiles changed in the level (see @ref ShowTileEditor())
		and saved into it then (see @ref ShowApplyTileChangesDialog()).
	*/
	void ShowNewTileSetDialog(EditorSession& session);

	/** @brief Makes a new tile set of empty tiles as @ref ShowNewTileSetDialog() does, returns `false` if it can't be */
	bool CreateEmptyTileSet(EditorSession& session, Death::Containers::StringView name, std::int32_t tileCount, bool addToLevel, bool asDefault);
}

#endif
