#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Canvas.h"
#include "../Font.h"
#include "../../Multiplayer/MpLevelHandler.h"

namespace Jazz2::UI::Multiplayer
{
	/** @brief In-game lobby screen for multiplayer */
	class MpInGameLobby : public Canvas
	{
	public:
		MpInGameLobby(Jazz2::Multiplayer::MpLevelHandler* levelHandler);
		~MpInGameLobby();

		void OnUpdate(float timeMult) override;
		bool OnDraw(RenderQueue& renderQueue) override;

		/** @brief Called when a touch event is triggered */
		void OnTouchEvent(const nCine::TouchEvent& event);

		/** @brief Returns `true` if the lobby screen is visible */
		bool IsVisible() const;
		/** @brief Shows the lobby screen */
		void Show();
		/** @brief Hides the lobby screen */
		void Hide();
		/** Sets allowed player types as bitmask of @ref PlayerType */
		void SetAllowedPlayerTypes(std::uint8_t playerTypes);

	private:
		static constexpr std::uint16_t MainLayer = 100;
		static constexpr std::uint16_t ShadowLayer = 80;
		static constexpr std::uint16_t FontLayer = 200;
		static constexpr std::uint16_t FontShadowLayer = 120;
		
		Jazz2::Multiplayer::MpLevelHandler* _levelHandler;
		Font* _smallFont;
		std::uint32_t _pressedActions;
		float _animation;
		std::int32_t _selectedPlayerType;
		std::uint8_t _allowedPlayerTypes;
		bool _isVisible;

		bool ActionPressed(PlayerAction action);
		bool ActionHit(PlayerAction action);
		void UpdatePressedActions();
	};
}

#endif