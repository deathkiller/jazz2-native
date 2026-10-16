#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

namespace Jazz2::Editor
{
	class EditorSession;

	/**
		@brief Shows the prefabs of the default tile set of the level, see @ref TilePrefab

		A prefab is chosen as the brush, the brush is saved as a new one under a name, or one is removed. The window
		stays open while the level is edited.
	*/
	void ShowPrefabsDialog(EditorSession& session);
}

#endif
