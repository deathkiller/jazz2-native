#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "TreasureHuntMode.h"

namespace Jazz2::Multiplayer::GameModes
{
	/**
		@brief Team Treasure Hunt game mode

		Like @ref TreasureHuntMode, but players are split into teams. The HUD is identical and inherited; only the
		team flag differs. Round logic is still handled by the host (see @ref TreasureHuntMode::OwnsRoundLogic).
	*/
	class TeamTreasureHuntMode : public TreasureHuntMode
	{
	public:
		MpGameMode GetMode() const override {
			return MpGameMode::TeamTreasureHunt;
		}
		bool IsTeamBased() const override {
			return true;
		}
	};
}

#endif
