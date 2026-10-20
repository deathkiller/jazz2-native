#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "TileTools.h"

#include <memory>

namespace Jazz2::Editor
{
	/**
		@brief Draws a line, the outline of a rectangle, or an ellipse with the stamp

		The shape is chosen in the options of the tool (see @ref ToolShape). A drag from one corner to the other shows
		the shape, it's painted when the pointer is let go, the right button erases it instead. <kbd>Shift</kbd> keeps
		a line at the steps of 45 degrees and a rectangle or an ellipse square. The stamp repeats from the corner the
		drag started at, so a pattern stays seamless, and the options of the tile tools apply as to the rectangle.
	*/
	class ShapeTool : public StrokeTool
	{
	public:
		explicit ShapeTool(IToolHost& host);

		EditorToolType GetType() const override;
		void OnDeactivate() override;
		void OnPointerDown(const ToolPointer& pointer) override;
		void OnPointerMove(const ToolPointer& pointer) override;
		void OnPointerUp(const ToolPointer& pointer) override;
		void Cancel() override;
		bool IsBusy() const override;
		void DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid) override;
		StringView GetHint() const override;
		void BuildOptions(ToolOptionsBuilder& builder) override;

		/** @brief Returns the cells of a shape drawn from one cell to another, each once */
		static void GetShapeCells(ToolShape shape, Vector2i from, Vector2i to, SmallVectorImpl<Vector2i>& cells);
		/** @brief Returns the end of a drag kept at the steps of 45 degrees for a line, or square for the other shapes */
		static Vector2i Constrain(ToolShape shape, Vector2i from, Vector2i to);

	private:
		bool _dragging;
		bool _erase;
		Vector2i _start;
		Vector2i _end;
	};
}

#endif
