#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "RaceMode.h"

namespace Jazz2::Multiplayer
{
	/**
		@brief Team Race game mode

		Like @ref RaceMode, but players race in teams (a team wins once all of its members have completed the
		required laps). The HUD is identical and inherited; only the team flag differs. Round logic is still handled
		by the host (see @ref RaceMode::OwnsRoundLogic), so the team win condition lives there for now.
	*/
	class TeamRaceMode : public RaceMode
	{
	public:
		MpGameMode GetMode() const override {
			return MpGameMode::TeamRace;
		}
		bool IsTeamBased() const override {
			return true;
		}
	};
}

#endif
