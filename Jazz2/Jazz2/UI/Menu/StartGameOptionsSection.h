#pragma once

#include "MenuSection.h"

namespace Jazz2::UI::Menu
{
	class StartGameOptionsSection : public MenuSection
	{
	public:
		StartGameOptionsSection(const StringView& episodeName, const StringView& levelName, const StringView& previousEpisodeName);

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

		String _episodeName;
		String _levelName;
		String _previousEpisodeName;

		ItemData _items[(int)Item::Count];
		int _selectedIndex;

		int _availableCharacters;
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