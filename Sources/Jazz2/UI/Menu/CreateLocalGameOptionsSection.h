#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "WidgetSection.h"
#include "../../Input/ControlScheme.h"
#include "../../Multiplayer/MpGameMode.h"

namespace Jazz2::UI::Menu
{
	/**
		@brief Local splitscreen game options menu section

		Lets the player configure a local splitscreen multiplayer match (number of players, per-player character,
		difficulty and game mode) before starting it. Built on the scrollable @ref WidgetSection so the variable
		number of rows always fits. The game mode is chosen through the shared @ref MultiplayerGameModeSelectSection,
		matching the create server screens.
	*/
	class CreateLocalGameOptionsSection : public WidgetSection
	{
	public:
		/**
		 * @brief Creates a new instance
		 *
		 * @param levelName            Level to start
		 * @param previousEpisodeName  Name of the episode the level belongs to
		 */
		CreateLocalGameOptionsSection(StringView levelName, StringView previousEpisodeName);

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawOverlay(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize) override;

		/** @brief Returns the selected multiplayer game mode */
		Jazz2::Multiplayer::MpGameMode GetGameMode() const {
			return _gameMode;
		}
		/** @brief Sets the selected multiplayer game mode */
		void SetGameMode(Jazz2::Multiplayer::MpGameMode value) {
			_gameMode = value;
		}

	private:
		String _levelName;
		String _previousEpisodeName;
		std::int32_t _availableCharacters;
		std::int32_t _playerCount;
		std::int32_t _playerTypes[ControlScheme::MaxSupportedPlayers];
		std::int32_t _currentPlayer;	// Player whose character row is currently focused (drives the left portrait)
		std::int32_t _difficulty;
		Jazz2::Multiplayer::MpGameMode _gameMode;
		Widget* _characterRows[ControlScheme::MaxSupportedPlayers];	// One focusable character row per player (shown/hidden by player count)
		bool _shouldStart;			// Start was confirmed; the fade-out transition is running before the level loads
		float _transitionTime;		// Remaining fade-out transition time

		void ChangePlayerCount(std::int32_t direction);
		void StartGame();
	};
}

#endif
