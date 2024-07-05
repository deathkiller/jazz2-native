#pragma once

#if defined(WITH_MULTIPLAYER)

#include "MenuSection.h"
#include "../../Multiplayer/MultiplayerGameMode.h"

namespace Jazz2::UI::Menu
{
	class CreateServerOptionsSection : public MenuSection
	{
	public:
		CreateServerOptionsSection(const StringView episodeName, const StringView levelName, const StringView previousEpisodeName);

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawOverlay(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize) override;

		void SetGameMode(Multiplayer::MultiplayerGameMode value);

	private:
		enum class Item {
			Character,
			GameMode,
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
		Multiplayer::MultiplayerGameMode _gameMode;

		ItemData _items[(std::int32_t)Item::Count];
		std::int32_t _selectedIndex;

		std::int32_t _availableCharacters;
		std::int32_t _selectedPlayerType;
		std::int32_t _selectedDifficulty;
		std::int32_t _lastPlayerType;
		std::int32_t _lastDifficulty;
		float _imageTransition;

		float _animation;
		float _transitionTime;
		bool _shouldStart;

		void ExecuteSelected();
		void OnAfterTransition();
		void StartImageTransition();
	};
}

#endif