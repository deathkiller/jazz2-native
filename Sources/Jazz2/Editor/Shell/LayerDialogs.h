#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

namespace Jazz2::Editor
{
	class EditorSession;

	/**
		@brief Shows a dialog to resize the layer being edited

		The new size is chosen with the side the layer grows or shrinks from, and what fills a layer that grows ---
		empty tiles, its edges going on, or the layer repeated.
	*/
	void ShowResizeLayerDialog(EditorSession& session);
}

#endif
