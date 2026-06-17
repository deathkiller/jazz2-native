#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MenuSection.h"

namespace Jazz2::UI::Menu
{
	/**
		@brief In-game multiplayer scoreboard

		Section of the in-game menu that lists all connected players with their per-round statistics (kills,
		deaths, points, ping and a mode-specific column) and team colors. Opened by pressing @ref PlayerAction::Up
		on the @ref PauseSection "Resume" item. The data rows live in a clipped, vertically scrollable band: it
		supports keyboard scrolling (Up/Down), smooth kinetic touch dragging with a scrollbar, and a tap (or
		@ref PlayerAction::Menu / @ref PlayerAction::Fire) to close.
	*/
	class ScoreboardSection : public MenuSection
	{
	public:
		/** @brief Creates a new instance */
		ScoreboardSection();

		Recti GetClipRectangle(const Recti& contentBounds) override;

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawClipped(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize) override;

	private:
		float _transition;
		std::int32_t _scrollY;			// Vertical scroll offset in pixels (<= 0; 0 = top), same convention as ScrollableMenuSection
		std::int32_t _contentHeight;	// Total height of all rows
		std::int32_t _availableHeight;	// Height of the visible (clipped) row band
		bool _scrollable;
		Vector2f _touchStart;
		Vector2f _touchLast;
		float _touchTime;
		float _touchSpeed;
		std::int8_t _touchDirection;
		bool _touchStartedAtBottom;	// Whether the scroll was already at the bottom when the current touch began (gates swipe-up-to-close)
	};
}

#endif
