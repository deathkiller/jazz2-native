#pragma once

#include "MenuSection.h"

namespace Jazz2::UI::Menu
{
	class RescaleModeSection : public MenuSection
	{
	public:
		RescaleModeSection();

		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize) override;

	private:
		enum class Item {
			None,
			HQ2x,
			_3xBrz,
			Crt,
			Monochrome,

			Count
		};

		struct ItemData {
			String Name;
			float TouchY;
		};

		ItemData _items[(int)Item::Count];
		int _selectedIndex;
		float _animation;

		void ExecuteSelected();
	};
}