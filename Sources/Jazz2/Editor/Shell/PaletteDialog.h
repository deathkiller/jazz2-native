#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

namespace Jazz2::Editor
{
	class EditorSession;

	/**
		@brief Shows the palette of the level, its colors can be changed while the level is seen in them

		A level is shown in the palette of its default tile set unless it has its own. Only its own can be changed,
		a level gets it as a copy of the palette it's shown in.
	*/
	void ShowLevelPaletteDialog(EditorSession& session);
}

#endif
