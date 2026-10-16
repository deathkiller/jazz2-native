#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "EditorTool.h"
#include "../Document/TerrainSplines.h"

#include <memory>

namespace Jazz2::Editor
{
	class UiElement;

	/**
		@brief Draws ground along a line, with slopes as steep as the line, and changes it later

		Each click adds a point of the surface, with <kbd>Ctrl</kbd> at a corner of a tile. The right button lays the
		ground as one step: the tiles the line goes through get pieces of ground of the tile set whose collision mask
		follows the line the closest (see @ref TileShapes), preferring those the levels use so; below them it's filled
		down to the ground that's there, at most a few tiles deep, and those tiles and the ones around are chosen by their
		neighbors (see @ref SmartTileModel).

		What is laid is chosen in the window of the tool (see @ref SplineKind): ground below the line, a ceiling above
		it, a floating band, or a closed shape whose points go around it. The line is straight between its points or a
		smooth curve through them.

		The line is kept with the level (see @ref TerrainSpline): its points can be dragged --- the ground follows while
		dragged, laid for good when let go ---, a click on the line adds one, <kbd>Shift</kbd>+click removes one, and the
		right button on the line removes the ground. Every change lays the ground again --- tiles it wrote get back what
		was there before, tiles changed by hand since stay as they are. Where the collision of the ground laid misses the
		line, its surface is drawn red.
	*/
	class GroundLineTool : public EditorTool
	{
	public:
		/** @brief Most tiles below the surface that are filled */
		static constexpr std::int32_t MaxDepth = 6;

		explicit GroundLineTool(IToolHost& host);
		~GroundLineTool() override;

		EditorToolType GetType() const override;
		void OnActivate() override;
		void OnDeactivate() override;
		void OnPointerDown(const ToolPointer& pointer) override;
		void OnPointerMove(const ToolPointer& pointer) override;
		void OnPointerUp(const ToolPointer& pointer) override;
		void Cancel() override;
		bool IsBusy() const override;
		void DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid) override;
		StringView GetHint() const override;

		/** @brief Returns the line last clicked or laid, of the lines of the level, or -1 */
		std::int32_t GetSelectedSpline() const {
			return _selectedSpline;
		}

		/**
		 * @brief Lays ground along a line again, or for the first time, or removes it, as one step
		 *
		 * @p index is the line in @p splines to change, -1 to add @p spline; @p spline is the line as it should be,
		 * or `nullptr` to remove it.
		 */
		static bool ApplySpline(IToolHost& host, SmallVectorImpl<TerrainSpline>& splines, std::int32_t index, const TerrainSpline* spline, StringView name);

		/**
		 * @brief Makes a line along the top of the selected ground, so ground that's there can be changed by it
		 *
		 * The line follows the collision masks of the topmost solid tiles of each column of the selection, and the solid
		 * tiles of the selection are its own, with nothing before them --- dragging the line lower takes ground away.
		 * Returns `false` if there's no ground selected.
		 */
		static bool TraceSelection(IToolHost& host);

		/** @brief Returns the line as fine points, straight between its points or along the curve through them, closed if it's a shape */
		static void SampleLine(const TerrainSpline& spline, SmallVectorImpl<Vector2f>& line);
		/** @brief Returns the height of a line at a position, of the fine points of a line that goes from left to right */
		static float GetLineY(ArrayView<const Vector2f> line, float x);

	private:
		struct Options
		{
			SplineKind Kind = SplineKind::Ground;
			bool Smooth = false;
			std::int32_t Depth = MaxDepth;
		};

		// The new line being drawn, in pixels of the layer
		SmallVector<Vector2f, 0> _points;
		// A point of a kept line being dragged
		std::int32_t _dragSpline;
		std::int32_t _dragPoint;
		// The point dragged is a new one, added to the line at _dragPoint
		bool _dragInsert;
		Vector2f _dragPos;
		// The line clicked last, which the options in the window change
		std::int32_t _selectedSpline;
		std::shared_ptr<Options> _options;
		std::shared_ptr<UiElement*> _window;
		// Tiles shown while a point is dragged, before the ground is laid for good
		std::int32_t _previewLayer;
		Recti _previewArea;
		Vector2f _previewPos;

		Vector2f GetPoint(const ToolPointer& pointer) const;
		bool HitTest(const SmallVectorImpl<TerrainSpline>& splines, const LayerView& view, Vector2f screen, std::int32_t& spline, std::int32_t& point, std::int32_t& segment) const;
		void Lay();
		void ShowWindow();
		void CloseWindow();
		// The line being dragged as it is with the dragged point
		bool GetDraggedSpline(const SmallVectorImpl<TerrainSpline>& splines, TerrainSpline& spline) const;
		void UpdatePreview();
		void ClearPreview();
		void ApplyOptionsToSelected();
	};
}

#endif
