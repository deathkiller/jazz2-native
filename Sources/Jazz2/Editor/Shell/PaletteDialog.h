#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <cstdint>

#include <Containers/String.h>

namespace Jazz2::Editor
{
	class IEditorContext;

	/**
		@brief Shows the palettes of the level, their colors can be changed while the level is seen in them

		The palette of the level, or the additional palette @p index, which layers can be drawn through (`SPRITE::MAPPING`
		of JJ2+), -1 for the palette of the level. A level is shown in the palette of its default tile set unless it has
		its own. Only its own can be changed, a level gets it as a copy of the palette it's shown in. Additional palettes
		can be added, renamed and removed. A dialog shown already is brought to the front and shows the palette.
	*/
	void ShowPalettesDialog(IEditorContext& session, std::int32_t index = -1);

	/** @brief Returns the name of the additional palette of an index, as the editor lists it, or an empty string */
	Death::Containers::String GetAdditionalPaletteLabel(IEditorContext& session, std::int32_t index);

	/**
		@brief Moves the entries @p first to @p last of the palette of the level an entry earlier or later, as one step

		With @p keepLook, the pixels of the tiles go with their colors, as the palette window moves them. Returns
		`false` if they can't be moved that way.
	*/
	bool MovePaletteEntries(IEditorContext& session, std::int32_t first, std::int32_t last, std::int32_t direction, bool keepLook);
}

#endif
