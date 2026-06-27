#pragma once

#include "MenuContainerBase.h"
#include "../../LevelHandler.h"

#include "../../../nCine/Input/InputEvents.h"

using namespace Jazz2::Tiles;

#if defined(WITH_MULTIPLAYER)
namespace Jazz2::Multiplayer
{
	class MpLevelHandler;
}
#endif

namespace Jazz2::UI::Menu
{
	/**
		@brief In-game menu

		@ref MenuContainerBase attached to a running @ref LevelHandler that hosts the menu sections shown while a game is
		paused, starting from @ref PauseSection. It draws over the live level and provides actions such as resuming the
		game, returning to the main menu, and (in multiplayer) spectating or reselecting a character.
	*/
	class InGameMenu : public MenuContainerBase
	{
		DEATH_RUNTIME_OBJECT(MenuContainerBase);

	public:
		/** @{ @name Constants */

		/** @brief Default width of viewport */
		static constexpr std::int32_t DefaultWidth = 720;
		/** @brief Default height of viewport */
		static constexpr std::int32_t DefaultHeight = 405;

		/** @} */

		/** @brief Creates a new instance attached to the specified level handler */
		InGameMenu(LevelHandler* root);
		~InGameMenu();

		/** @brief Called when a key is pressed */
		void OnKeyPressed(const nCine::KeyboardEvent& event);
		/** @brief Called when a key is released */
		void OnKeyReleased(const nCine::KeyboardEvent& event);
		/** @brief Called when a touch event is triggered */
		void OnTouchEvent(const nCine::TouchEvent& event);
		/** @brief Called when the viewport needs to be initialized (e.g., when the resolution is changed) */
		void OnInitializeViewport(std::int32_t width, std::int32_t height);

		void ChangeLevel(LevelInitialization&& levelInit) override;
		bool HasResumableState() const override;
		void ResumeSavedState() override;
#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)
		void ConnectToServer(StringView endpoint, std::uint16_t defaultPort) override;
		bool CreateServer(Jazz2::Multiplayer::ServerInitialization&& serverInit) override;
#endif
		void ApplyPreferencesChanges(ChangedPreferencesType type) override;

		Vector2i GetViewSize() const override {
			return _canvasBackground->ViewSize;
		}

		/** @brief Returns `true` if the level handler is on a local session */
		bool IsLocalSession() const;
		/** @brief Hides the in-game menu and resumes paused game */
		void ResumeGame();
		/** @brief Leaves the paused game and switches back to the main menu */
		void GoToMainMenu();

#if defined(WITH_MULTIPLAYER)
		/** @brief Returns `true` if the player is currently spectating */
		bool IsSpectating();
		/** @brief Returns `true` if spectate mode is enabled by the server */
		bool IsSpectateAvailable();
		/** @brief Enters spectate mode */
		void EnterSpectate();
		/** @brief Opens the in-game lobby to (re)select a character, (re)joining the game on confirmation */
		void ShowCharacterSelect();
		/** @brief Returns the multiplayer level handler if this is an online session, otherwise `nullptr` (used by the scoreboard) */
		Jazz2::Multiplayer::MpLevelHandler* GetMultiplayerHandler();
#endif

	private:
		LevelHandler* _root;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class MenuBackgroundCanvas : public Canvas
		{
		public:
			MenuBackgroundCanvas(InGameMenu* owner) : _owner(owner) { }

			void OnUpdate(float timeMult) override;
			bool OnDraw(RenderQueue& renderQueue) override;

		private:
			InGameMenu* _owner;

			void DrawViewportSeparators();
		};

		class MenuClippedCanvas : public Canvas
		{
		public:
			MenuClippedCanvas(InGameMenu* owner) : _owner(owner) { }

			bool OnDraw(RenderQueue& renderQueue) override;

		private:
			InGameMenu* _owner;
		};

		class MenuOverlayCanvas : public Canvas
		{
		public:
			MenuOverlayCanvas(InGameMenu* owner) : _owner(owner) { }

			bool OnDraw(RenderQueue& renderQueue) override;

		private:
			InGameMenu* _owner;
		};
#endif

	protected:
		Rendering::UpscaleRenderPassWithClipping& GetUpscalePass() override {
			return _root->GetActiveOverlayPass();
		}
		const BitArray& GetPressedKeys() const override {
			return _root->_pressedKeys;
		}
	};
}
