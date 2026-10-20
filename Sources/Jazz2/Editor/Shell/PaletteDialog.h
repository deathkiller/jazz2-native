#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <cstdint>

namespace Jazz2::Editor
{
	class IEditorContext;

	/**
		@brief Shows the palette of the level, its colors can be changed while the level is seen in them

		A level is shown in the palette of its default tile set unless it has its own. Only its own can be changed,
		a level gets it as a copy of the palette it's shown in.
	*/
	void ShowLevelPaletteDialog(IEditorContext& session);

	/**
		@brief Moves the entries @p first to @p last of the palette of the level an entry earlier or later, as one step

		With @p keepLook, the pixels of the tiles go with their colors, as the palette window moves them. Returns
		`false` if they can't be moved that way.
	*/
	bool MovePaletteEntries(IEditorContext& session, std::int32_t first, std::int32_t last, std::int32_t direction, bool keepLook);
}

#endif
