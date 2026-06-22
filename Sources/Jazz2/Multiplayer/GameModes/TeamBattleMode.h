#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "BattleMode.h"

namespace Jazz2::Multiplayer
{
	/**
		@brief Team Battle game mode

		Like @ref BattleMode, but players are split into teams. The per-player kill/death and respawn rules are
		identical (inherited), so only the win condition differs: the round is won by the first team to reach the
		kill target (its members' kills combined) or, with elimination enabled, by the last team that still has a
		member within the death limit.
	*/
	class TeamBattleMode : public BattleMode
	{
	public:
		MpGameMode GetMode() const override {
			return MpGameMode::TeamBattle;
		}
		bool IsTeamBased() const override {
			return true;
		}

		GameEndResult CheckGameEnds(IGameModeContext& ctx) override;
	};
}

#endif
