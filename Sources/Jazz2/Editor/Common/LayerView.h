#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../nCine/Input/InputEvents.h"
#include "../../../nCine/Primitives/Rect.h"
#include "../../../nCine/Primitives/Vector2.h"

#include <cstdint>

using namespace nCine;

namespace Jazz2::Editor
{
	/**
		@brief How a layer is shown in the view

		Layers other than the sprite layer move at their own speed, so a position in the view lies on another tile of
		each layer. Cells here are counted from the tile (0, 0) of the layer as it's shown, without repeating ---
		@ref Wrap() finds the tile of the layer a cell shows.
	*/
	struct LayerView
	{
		/** @brief Position of the tile (0, 0) of the layer in the level */
		Vector2f Origin;
		/** @brief Size of the layer in tiles */
		Vector2i Size;
		/** @brief Whether the layer repeats horizontally */
		bool RepeatX = false;
		/** @brief Whether the layer repeats vertically */
		bool RepeatY = false;
		/** @brief Pixels of the screen one pixel of the level takes */
		float Scale = 1.0f;
		/** @brief Position of the top left corner of the view in pixels of the level multiplied by the scale */
		Vector2f ViewOrigin;
		/** @brief Position of the view on the screen */
		Vector2f ScreenOffset;

		/** @brief Converts a position on the screen to a position in the level */
		Vector2f ScreenToWorld(Vector2f screen) const;
		/** @brief Converts a position in the level to a position on the screen */
		Vector2f WorldToScreen(Vector2f world) const;
		/** @brief Returns the cell at a position in the level */
		Vector2i WorldToCell(Vector2f world) const;
		/** @brief Returns the rectangle a cell takes on the screen */
		Rectf CellToScreen(Vector2i cell) const;
		/** @brief Returns the tile of the layer a cell shows, `false` if it shows none */
		bool Wrap(Vector2i cell, Vector2i& tile) const;
	};

	/** @brief Pointer over the view, as a tool sees it */
	struct ToolPointer
	{
		/** @brief Position on the screen */
		Vector2f Screen;
		/** @brief Position in the level */
		Vector2f World;
		/** @brief Cell of the current layer, see @ref LayerView */
		Vector2i Cell;
		/** @brief Button pressed or released */
		MouseButton Button = MouseButton::Left;
		/** @brief Modifier keys held */
		std::int32_t Modifiers = 0;
	};
}

#endif
