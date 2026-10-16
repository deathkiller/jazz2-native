#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "EditorTool.h"

#include <memory>

namespace Jazz2::Editor
{
	class UiElement;

	/**
		@brief Places the event to place along a path, at even distances --- a row or an arc of coins or gems

		Each click adds a point of the path, with <kbd>Ctrl</kbd> in the middle of a tile. The path goes straight
		from point to point, or in a smooth curve through them. The events are shown where they'll be, the right
		button places them, as one step that can be undone. They're placed off the grid, exactly on the path, or on
		the tiles the path goes through. A window next to the view sets the distance between them.
	*/
	class PathTool : public EditorTool
	{
	public:
		/** @brief How the events are placed */
		struct Options
		{
			/** @brief Distance between two events in pixels */
			float Spacing = 32.0f;
			/** @brief Whether the path is a smooth curve through the points */
			bool Smooth = true;
			/** @brief Whether the events are placed on the tiles, otherwise off the grid */
			bool OnGrid = false;
		};

		explicit PathTool(IToolHost& host);
		~PathTool() override;

		EditorToolType GetType() const override;
		void OnActivate() override;
		void OnDeactivate() override;
		void OnPointerDown(const ToolPointer& pointer) override;
		void Cancel() override;
		bool IsBusy() const override;
		void DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid) override;
		StringView GetHint() const override;

		/** @brief Returns a path as a fine line, straight from point to point or a smooth curve through them */
		static void GetLine(ArrayView<const Vector2f> points, bool smooth, SmallVectorImpl<Vector2f>& line);
		/** @brief Returns where events go along a path, at even distances from its first point */
		static void GetPlacements(ArrayView<const Vector2f> points, bool smooth, float spacing, SmallVectorImpl<Vector2f>& placements);

	private:
		SmallVector<Vector2f, 0> _points;
		std::shared_ptr<Options> _options;
		// The window of the options while it's shown, shared with it, it's closed by the user too
		std::shared_ptr<UiElement*> _window;

		Vector2f GetPoint(const ToolPointer& pointer) const;
		void ShowWindow();
		void CloseWindow();
		void Place();
	};
}

#endif
