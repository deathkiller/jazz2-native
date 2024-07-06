#pragma once

#include "MenuSection.h"
#include "../ControlScheme.h"

namespace Jazz2::UI::Menu
{
	class StartGameOptionsSection : public MenuSection
	{
	public:
		StartGameOptionsSection(const StringView episodeName, const StringView levelName, const StringView previousEpisodeName);

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawOverlay(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize) override;

	private:
		enum class Item {
			PlayerCount,
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

		ItemData _items[(std::int32_t)Item::Count];
		std::int32_t _selectedIndex;

		std::int32_t _availableCharacters;
		std::int32_t _playerCount;
		std::int32_t _lastPlayerType;
		std::int32_t _lastDifficulty;
		std::int32_t _selectedPlayerType[ControlScheme::MaxSupportedPlayers];
		std::int32_t _selectedDifficulty;
		float _imageTransition;

		float _animation;
		float _transitionTime;
		bool _shouldStart;

		void ExecuteSelected();
		void OnAfterTransition();
		void StartImageTransition();
	};
}