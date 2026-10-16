#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Document/LevelDocument.h"

namespace Jazz2::Editor
{
	class EditorSession;

	/**
		@brief Shows a window to change the pixels and the collision mask of a tile, only in this level

		The changes are kept in the level as overrides of the tile, its tile set stays as it is, and the level shows
		them right away. A true color tile is reduced to the palette of the level, the overrides keep palette indices.
	*/
	void ShowTileEditor(EditorSession& session, const TileRef& tile);
}

#endif
