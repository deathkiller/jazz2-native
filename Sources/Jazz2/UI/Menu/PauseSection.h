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

		struct ItemData {
			String Name;
			float TouchY;
		};

		ItemData _items[(std::int32_t)Item::Count];
		std::int32_t _selectedIndex;
		float _animation;

		void ExecuteSelected();
	};
}