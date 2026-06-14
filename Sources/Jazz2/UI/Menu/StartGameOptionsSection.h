#pragma once

#include "MenuSection.h"
#include "../../Input/ControlScheme.h"

namespace Jazz2::UI::Menu
{
	/**
		@brief Start game options menu section
		
		Lets the player choose the number of local players, the characters, and the difficulty before starting the
		selected episode or level.
	*/
	class StartGameOptionsSection : public MenuSection
	{
	public:
		/**
		 * @brief Creates a new instance
		 *
		 * @param levelName            Level to start
		 * @param previousEpisodeName  Name of the episode the level belongs to
		 */
		StartGameOptionsSection(StringView levelName, StringView previousEpisodeName);

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawOverlay(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize) override;

	private:
		enum class Item {
			PlayerCount,
			Difficulty,
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