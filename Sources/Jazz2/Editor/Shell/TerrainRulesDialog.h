#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

namespace Jazz2::Editor
{
	class EditorSession;

	/**
		@brief Shows the tiles ground gets by its neighbors, and lets them be chosen by hand, see @ref TerrainRules

		Each of the 47 ways the cells around can be solid shows the tile the tools choose for it --- learned from the
		levels of the tile set, or chosen by hand, framed then. A click makes the tiles of the brush the tiles for it, more
		of them are taken in turns, the right button returns it to the learned one. The window stays open while the
		level is edited.
	*/
	void ShowTerrainRulesDialog(EditorSession& session);
}

#endif
