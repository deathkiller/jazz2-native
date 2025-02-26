#pragma once

#include "MenuSection.h"

namespace Jazz2::UI::Menu
{
	/** @brief In-game menu root section */
	class PauseSection : public MenuSection
	{
	public:
		PauseSection();

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize) override;

	private:
		enum class Item {
			Resume,
			Options,
			Exit,

			Count
		};

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct ItemData {
			String Name;
			float TouchY;
		};
#endif

		ItemData _items[(std::int32_t)Item::Count];
		std::int32_t _selectedIndex;
		float _animation;

		void ExecuteSelected();
	};
}