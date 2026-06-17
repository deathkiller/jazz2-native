#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MenuSection.h"

namespace Jazz2::UI::Menu
{
	/**
		@brief In-game multiplayer scoreboard

		Section of the in-game menu that lists all connected players with their per-round statistics (kills,
		deaths, points, ping and a mode-specific column) and team colors. Opened by pressing @ref PlayerAction::Up
		on the @ref PauseSection "Resume" item; scrolls with Up/Down when there are more rows than fit and returns
		to the previous section once scrolled past either end.
	*/
	class ScoreboardSection : public MenuSection
	{
	public:
		/** @brief Creates a new instance */
		ScoreboardSection();

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize) override;

	private:
		std::int32_t _scrollOffset;
		float _transition;

		std::int32_t GetVisibleRows() const;
	};
}

#endif
