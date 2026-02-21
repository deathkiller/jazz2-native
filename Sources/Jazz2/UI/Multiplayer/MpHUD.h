#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../HUD.h"
#include "../../Multiplayer/MpLevelHandler.h"

namespace Jazz2::UI::Multiplayer
{
	/** @brief Player HUD for multiplayer */
	class MpHUD : public HUD
	{
	public:
		MpHUD(Jazz2::Multiplayer::MpLevelHandler* levelHandler);

		void OnUpdate(float timeMult) override;
		bool OnDraw(RenderQueue& renderQueue) override;

		/** @brief Shows countdown text in the middle of the screen */
		void ShowCountdown(std::int32_t secsLeft);

	protected:
		void OnDrawOverview(const Rectf& view, const Rectf& adjustedView, Actors::Player* player) override;
		void OnDrawScore(const Rectf& view, Actors::Player* player) override;

		void DrawPositionInRound(const Rectf& view, Actors::Player* player);

	private:
		Font* _mediumFont;
		String _countdownText;
		float _countdownTimeLeft;
	};
}

#endif