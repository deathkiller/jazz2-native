#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "EditorTool.h"

namespace Jazz2::Editor
{
	/**
		@brief Measures distances in the level and shows how far the player jumps

		A drag measures from where it starts to where it ends, in pixels and in tiles, and shows the arcs of the jumps
		chosen in the options of the tool --- walking, dashing, the double jump of Spaz and the copter of Jazz --- from its
		start, where the feet of the player are, towards its end. They jump as the game does with the
		gameplay it's set to: the original game in its ticks (see @ref Actors::Player::LegacyJumpSpeed and the other
		`Legacy` constants, the movement applied in a tick held to 8 px), or the Reforged jump that rises on an internal
		force, in frames at 60 FPS. It tells the easiest of them that gets over to the end. The jumps stop at the solid
		tiles of the sprite layer and end where they land, unless that's turned off. Started on a spring, it shows how
		the spring throws the player instead, how high or how far, roughly. With <kbd>Ctrl</kbd> both ends snap to the
		middle of a tile. The measurement stays until the next one, the right button clears it.
	*/
	class MeasureTool : public EditorTool
	{
	public:
		/** @brief One of the jumps the tool shows */
		struct JumpArc
		{
			/** @brief Horizontal speed in px per tick of the original, 70 a second, or per frame at 60 FPS in Reforged */
			float SpeedX;
			/** @brief Whether Spaz jumps again at the top */
			bool DoubleJump;
			/** @brief Whether it's the jump of the Reforged gameplay */
			bool Reforged;
			/** @brief Whether Jazz glides down from the top with the copter */
			bool Copter = false;
			/** @brief Strength of a spring that throws the player instead of a jump, 0 for a jump */
			float Spring = 0.0f;
			/** @brief Whether the spring throws the player along instead of up */
			bool SpringHorizontal = false;
		};

		explicit MeasureTool(IToolHost& host);

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

		/**
		 * @brief Returns the path of a jump, a point each tick, until it lands or falls far below its start
		 *
		 * @p start is where the feet of the player are, @p direction 1 to the right, -1 to the left. With
		 * @p collisions the path stops at walls and ceilings of the sprite layer and ends where it lands.
		 */
		static void GetJumpPath(const JumpArc& arc, Vector2f start, float direction, SmallVectorImpl<Vector2f>& path, Tiles::TileMap* collisions = nullptr);
		/** @brief Returns `true` if a jump gets over to a point, landing on it or above it */
		static bool Reaches(const JumpArc& arc, Vector2f start, Vector2f target, Tiles::TileMap* collisions = nullptr);

	private:
		Vector2f _start;
		Vector2f _end;
		bool _hasMeasure;
		bool _dragging;

		Vector2f GetPoint(const ToolPointer& pointer) const;
	};
}

#endif
