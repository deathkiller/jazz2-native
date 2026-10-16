#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "EditorTool.h"

namespace Jazz2::Editor
{
	/**
		@brief Measures distances in the level and shows how far the player jumps

		A drag measures from where it starts to where it ends, in pixels and in tiles, and shows the arcs of a walking
		jump, a dashing jump and the double jump of Spaz from its start towards its end --- as the original game jumps,
		in its ticks (see @ref Actors::Player::LegacyJumpSpeed and the other `Legacy` constants). It tells which of them
		gets over to the end, what's in the way isn't taken into account. With <kbd>Ctrl</kbd> both ends snap to the
		middle of a tile. The measurement stays until the next one, the right button clears it.
	*/
	class MeasureTool : public EditorTool
	{
	public:
		/** @brief One of the jumps the tool shows */
		struct JumpArc
		{
			/** @brief Horizontal speed in px per tick of the original, 70 a second */
			float SpeedX;
			/** @brief Whether Spaz jumps again at the top */
			bool DoubleJump;
		};

		explicit MeasureTool(IToolHost& host);

		EditorToolType GetType() const override;
		void OnPointerDown(const ToolPointer& pointer) override;
		void OnPointerMove(const ToolPointer& pointer) override;
		void OnPointerUp(const ToolPointer& pointer) override;
		void Cancel() override;
		bool IsBusy() const override;
		void DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid) override;
		StringView GetHint() const override;

		/**
		 * @brief Returns the path of a jump, a point each tick of the original, until it falls far below its start
		 *
		 * @p direction is 1 to the right, -1 to the left.
		 */
		static void GetJumpPath(const JumpArc& arc, Vector2f start, float direction, SmallVectorImpl<Vector2f>& path);
		/** @brief Returns `true` if a jump gets over to a point, landing on it or above it */
		static bool Reaches(const JumpArc& arc, Vector2f start, Vector2f target);

	private:
		Vector2f _start;
		Vector2f _end;
		bool _hasMeasure;
		bool _dragging;

		Vector2f GetPoint(const ToolPointer& pointer) const;
	};
}

#endif
