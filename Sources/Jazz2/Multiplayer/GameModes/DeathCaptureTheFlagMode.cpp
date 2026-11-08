#include "DeathCaptureTheFlagMode.h"

#if defined(WITH_MULTIPLAYER)

#include "../ServerInitialization.h"

namespace Jazz2::Multiplayer::GameModes
{
	GameEndResult DeathCaptureTheFlagMode::CheckGameEnds(IGameModeContext& ctx)
	{
		// The team score is the captures plus the roasts, see MpLevelHandler::GetTeamScore()
		const auto& config = ctx.GetServerConfiguration();
		std::uint8_t teamCount = ctx.GetTeamCount();
		for (std::uint8_t team = 0; team < teamCount; team++) {
			if (ctx.GetTeamScore(team) >= config.TotalKills) {
				return GameEndResult::TeamWins(team);
			}
		}
		return GameEndResult::None();
	}

	std::uint32_t DeathCaptureTheFlagMode::GetRoundScore(IGameModeContext& ctx, Actors::Player* player)
	{
		return ctx.GetPlayerState(player).Kills;
	}
}

#endif
