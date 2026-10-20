#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Document/LevelDocument.h"

namespace Jazz2::Editor
{
	class IEditorContext;

	/**
		@brief Shows a dialog to resize the layer being edited

		The new size is chosen with the side the layer grows or shrinks from, and what fills a layer that grows ---
		empty tiles, its edges going on, or the layer repeated.
	*/
	void ShowResizeLayerDialog(IEditorContext& session);

	/**
		@brief Resizes a layer as one step that can be undone, as the dialog does, see @ref LevelDocument::ResizeLayer()

		The tile (0, 0) of the layer goes to @p offset. Its ground lines move with its tiles, and with the sprite layer
		the events, the water level, the bookmarks and the notes. Shows a message and returns `false` if the layer can't
		be that large.
	*/
	bool ResizeLayerAsStep(IEditorContext& session, std::int32_t layer, Vector2i size, Vector2i offset, LevelDocument::ResizeFill fill);
}

#endif
