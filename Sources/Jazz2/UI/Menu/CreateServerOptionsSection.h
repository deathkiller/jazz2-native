#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MenuSection.h"
#include "../../Multiplayer/MpGameMode.h"

namespace Jazz2::UI::Menu
{
	class CreateServerOptionsSection : public MenuSection
	{
	public:
		CreateServerOptionsSection(StringView levelName, StringView previousEpisodeName, bool privateServer);

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawOverlay(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize) override;

		void SetGameMode(Jazz2::Multiplayer::MpGameMode value);

	private:
		enum class Item {
			Character,
			GameMode,
			Start,

			Count
		};

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct ItemData {
			String Name;
			float TouchY;
		};
#endif

		String _levelName;
		String _previousEpisodeName;
		Jazz2::Multiplayer::MpGameMode _gameMode;

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
		bool _privateServer;
		bool _shouldStart;

		void ExecuteSelected();
		void OnAfterTransition();
		void StartImageTransition();
	};
}

#endif