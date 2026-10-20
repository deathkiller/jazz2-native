#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

namespace Jazz2::Editor
{
	class IEditorContext;

	/** @brief Shows the properties of the level --- its name, the levels that follow, music, light, weather and flags */
	void ShowLevelPropertiesDialog(IEditorContext& session);
	/** @brief Shows the texts of the level, which events show and refer to by their index */
	void ShowLevelTextsDialog(IEditorContext& session);
	/** @brief Shows the animated tiles of the level, their frames and timing */
	void ShowAnimatedTilesDialog(IEditorContext& session);
}

#endif
