#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MenuSection.h"
#include "../../Multiplayer/MpGameMode.h"

namespace Jazz2::UI::Menu
{
	/**
		@brief Create server options menu section
		
		Lets the player configure the character and game mode for a hosted multiplayer server before starting it.
	*/
	class CreateServerOptionsSection : public MenuSection
	{
	public:
		/** @brief Special value for LevelName to create a server from configured playlist */
		static constexpr StringView FromPlaylist = "\0:playlist"_s;

		/**
		 * @brief Creates a new instance
		 *
		 * @param levelName            Level to host, or @ref FromPlaylist to use the configured playlist
		 * @param previousEpisodeName  Name of the episode the level belongs to
		 * @param privateServer        Whether the server should be private (not publicly listed)
		 */
		CreateServerOptionsSection(StringView levelName, StringView previousEpisodeName, bool privateServer);

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawOverlay(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize) override;

		/** @brief Returns the selected multiplayer game mode */
		Jazz2::Multiplayer::MpGameMode GetGameMode() const;
		/** @brief Sets the selected multiplayer game mode */
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