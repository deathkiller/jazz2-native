#pragma once

#include "MenuSection.h"

namespace Jazz2::UI::Menu
{
	class GameplayEnhancementsSection : public MenuSection
	{
	public:
		GameplayEnhancementsSection();
		~GameplayEnhancementsSection();

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize) override;

	private:
		enum class Item {
			Reforged,
			LedgeClimb,
			WeaponWheel,

			Count
		};

		struct ItemData {
			String Name;
			float TouchY;
		};

		ItemData _items[(int)Item::Count];
		int _selectedIndex;
		float _animation;
		float _transition;
		bool _isDirty;
		bool _isInGame;

		void ExecuteSelected();
	};
}