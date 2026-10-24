#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <Containers/StringView.h>

#include <cstdint>

namespace Jazz2::Formats
{
	struct TileSetData;
}

namespace Jazz2::Editor
{
	class IEditorContext;

	/**
		@brief Asks for an image and makes a new tile set of it, in the user content

		The image is cut into tiles of 32 × 32 pixels row by row, an image of the masks next to it (`.mask.png`, as
		the editor exports them) gives their masks, otherwise they're solid where they aren't transparent. The new tile
		set can be added to the level right away.
	*/
	void ShowImportTileSetDialog(IEditorContext& session);

	/**
	 * @brief Makes a new tile set of an image as @ref ShowImportTileSetDialog() does, for testing
	 *
	 * @p colors is `own`, `true`, `match` or `reduce`, empty keeps the colors as they are.
	 */
	void ImportTileSetImage(IEditorContext& session, Death::Containers::StringView path, Death::Containers::StringView name, bool addToLevel, bool asDefault,
		Death::Containers::StringView colors = {}, bool dither = false);

	/**
		@brief Asks for a name and a number of tiles and makes a new tile set of empty tiles, in the user content

		It has the palette of the level. Its tiles are painted as tiles changed in the level (see @ref ShowTileEditor())
		and saved into it then (see @ref ShowApplyTileChangesDialog()).
	*/
	void ShowNewTileSetDialog(IEditorContext& session);

	/** @brief Makes a new tile set of empty tiles as @ref ShowNewTileSetDialog() does, returns `false` if it can't be */
	bool CreateEmptyTileSet(IEditorContext& session, Death::Containers::StringView name, std::int32_t tileCount, bool addToLevel, bool asDefault);

	/**
		@brief Asks for a tile set file and saves it into the user content as a new tile set

		A tile set of the original game (`.j2t`) is converted, as the game converts it, one of this game is copied. It
		opens in the folder of the original game, any other place can be browsed too. The new tile set can be added to
		the level right away.
	*/
	void ShowImportTileSetFileDialog(IEditorContext& session);

	/**
	 * @brief Reads a whole tile set file of the original game or of this game, see @ref ShowImportTileSetFileDialog()
	 *
	 * @p original is set if it's one of the original game. Returns `false` if the file isn't a tile set of either.
	 */
	bool ReadTileSetFile(Death::Containers::StringView path, Formats::TileSetData& data, bool& original);

	/** @brief Saves a tile set into the user content as @ref ShowImportTileSetFileDialog() does, returns `false` if it can't be */
	bool ImportTileSetFile(IEditorContext& session, const Formats::TileSetData& data, Death::Containers::StringView name, bool addToLevel, bool asDefault);
}

#endif
