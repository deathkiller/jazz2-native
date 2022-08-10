#pragma once

#include "MenuSection.h"

namespace Jazz2::UI::Menu
{
	class StartGameOptionsSection : public MenuSection
	{
	public:
		StartGameOptionsSection();

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize) override;

	private:
		enum class Item {
			Character,
			Difficulty,
			Start,

			Count
		};

		struct ItemData {
			String Name;
			float TouchY;
		};

		ItemData _items[(int)Item::Count];
		int _selectedIndex;

		int _selectedPlayerType;
		int _selectedDifficulty;
		int _lastPlayerType;
		int _lastDifficulty;
		float _imageTransition;

		float _animation;

		void ExecuteSelected();
		void StartImageTransition();
	};
}