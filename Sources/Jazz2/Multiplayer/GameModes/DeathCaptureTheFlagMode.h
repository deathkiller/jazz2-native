#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "CaptureTheFlagMode.h"

namespace Jazz2::Multiplayer::GameModes
{
	/**
		@brief Death Capture The Flag game mode (team-based)

		@ref CaptureTheFlagMode where every roast of an enemy scores a point for the team too. A capture is worth a
		point plus one for every enemy player, and then everybody dies and starts over. The flags themselves are
		handled by the host the same way as in Capture The Flag. The first team to reach the score wins.
	*/
	class DeathCaptureTheFlagMode : public CaptureTheFlagMode
	{
	public:
		MpGameMode GetMode() const override {
			return MpGameMode::DeathCaptureTheFlag;
		}
		bool OwnsRoundLogic() const override {
			return true;
		}

		GameEndResult CheckGameEnds(IGameModeContext& ctx) override;
		std::uint32_t GetRoundScore(IGameModeContext& ctx, Actors::Player* player) override;
	};
}

#endif
