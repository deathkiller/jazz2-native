#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "EditorTool.h"

#include <Containers/Function.h>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::Function;
#endif

	/**
		@brief Paints the pixels of a tile right in the level, for the tile editor

		Every cell of the layer being edited that shows the tile is outlined, a stroke over any of them paints the
		pixels under the pointer --- flipped as the cell shows the tile, so a pixel goes where it's seen. All the
		other places of the level that show the tile change with it. What is painted is up to the tile editor, see
		@ref SetTarget(), the tool only finds the pixel.
	*/
	class TilePixelTool : public EditorTool
	{
	public:
		/**
		 * @brief Paints a line of pixels of the tile
		 *
		 * Gets the first and the last pixel of the line, the same one for a single pixel, the button and whether the
		 * stroke goes on --- the first line of a stroke is a new step of the history, the others merge into it. A
		 * pointer moved fast over the same cell gives a line, so the stroke has no gaps.
		 */
		using PaintFunction = Function<void(Vector2i, Vector2i, MouseButton, bool)>;

		explicit TilePixelTool(IToolHost& host);

		EditorToolType GetType() const override;
		void OnPointerDown(const ToolPointer& pointer) override;
		void OnPointerMove(const ToolPointer& pointer) override;
		void OnPointerUp(const ToolPointer& pointer) override;
		void Cancel() override;
		bool IsBusy() const override;
		void DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid) override;
		StringView GetHint() const override;

		/** @brief Sets the tile, by its ID as the level file stores it, and what paints it, `nullptr` to stop */
		void SetTarget(std::int32_t tileId, PaintFunction&& paint);
		/** @brief Returns the tile painted, or -1 */
		std::int32_t GetTargetTile() const {
			return _tileId;
		}

	private:
		std::int32_t _tileId;
		PaintFunction _paint;
		bool _painting;
		MouseButton _button;
		Vector2i _lastPixel;
		// The cell the last pixel was painted in, a line to the next one is drawn only within the same cell
		Vector2i _lastCell;

		// The pixel of the tile under a point of the level, `false` if the cell doesn't show the tile
		bool GetPixel(const ToolPointer& pointer, Vector2i& pixel) const;
		bool ShowsTile(std::int32_t layer, Vector2i tile, std::uint8_t& flags) const;
	};
}

#endif
