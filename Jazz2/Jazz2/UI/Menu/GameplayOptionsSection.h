#pragma once

#include "MenuSection.h"

namespace Jazz2::UI::Menu
{
	class GameplayOptionsSection : public MenuSection
	{
	public:
		GameplayOptionsSection();
		~GameplayOptionsSection();

		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize) override;

	private:
		enum class Item {
			Enhancements,
			EnableRgbLights,

			Count
		};

		struct ItemData {
			String Name;
			float TouchY;
		};

		ItemData _items[(int)Item::Count];
		int _selectedIndex;
		float _animation;
		bool _isDirty;

		void ExecuteSelected();
	};
}