#include "TeamBattleMode.h"

#if defined(WITH_MULTIPLAYER)

#include "../ServerInitialization.h"
#include "../Teams.h"
#include "../../Actors/Player.h"

namespace Jazz2::Multiplayer::GameModes
{
	GameEndResult TeamBattleMode::CheckGameEnds(IGameModeContext& ctx)
	{
		const auto& config = ctx.GetServerConfiguration();
		std::uint8_t teamCount = ctx.GetTeamCount();

		if (config.Elimination) {
			// Team elimination: a team is out when all its non-spectating members are out; the last team wins.
			// Faithful replication of the team-elimination branch in the original MpLevelHandler::CheckGameEnds.
			bool teamAlive[MaxTeamCount] = {};
			for (auto* player : ctx.GetPlayers()) {
				auto& state = ctx.GetPlayerState(player);
				if (ctx.IsSpectating(player) || state.Team >= teamCount) {
					continue;
				}
				if (state.Deaths < config.TotalKills) {
					teamAlive[state.Team] = true;
				}
			}

			std::int32_t aliveTeams = 0;
			std::uint8_t lastAliveTeam = 0;
			for (std::uint8_t team = 0; team < teamCount; team++) {
				if (teamAlive[team]) {
					aliveTeams++;
					lastAliveTeam = team;
				}
			}

			if (aliveTeams <= 1) {
				return { true, true, nullptr, lastAliveTeam };
			}
			return GameEndResult::None();
		}

		// First team to reach the kill target (combined member kills) wins
		for (std::uint8_t team = 0; team < teamCount; team++) {
			if (ctx.GetTeamScore(team) >= config.TotalKills) {
				return { true, true, nullptr, team };
			}
		}
		return GameEndResult::None();
	}
}

#endif
