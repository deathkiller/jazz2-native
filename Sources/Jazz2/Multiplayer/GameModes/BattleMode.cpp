#include "BattleMode.h"

#if defined(WITH_MULTIPLAYER)

#include "../ServerInitialization.h"
#include "../../Actors/Player.h"
#include "../../UI/Font.h"

#include <algorithm>

#include <Base/Format.h>

namespace Jazz2::Multiplayer::GameModes
{
	void BattleMode::OnRoundStarted(IGameModeContext& ctx)
	{
		// Per-round counters are reset universally by the host (ResetAllPlayerStats), so Battle has no extra
		// round-start setup of its own.
	}

	void BattleMode::OnUpdate(IGameModeContext& ctx, float timeMult)
	{
		// Nothing mode-specific to tick (win condition is checked on death)
	}

	void BattleMode::OnPlayerSpawned(IGameModeContext& ctx, Actors::Player* player)
	{
		// Nothing mode-specific on spawn (invulnerability is applied via DecideRespawn)
	}

	void BattleMode::OnPlayerKilled(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* killer)
	{
		// Kill and death counts are maintained universally by the host (every mode counts them the same way), so
		// Battle has no additional on-kill reaction to perform here.
	}

	RespawnDecision BattleMode::DecideRespawn(IGameModeContext& ctx, Actors::Player* player)
	{
		const auto& config = ctx.GetServerConfiguration();
		auto& state = ctx.GetPlayerState(player);

		RespawnDecision decision;
		// With elimination, a player is out once they reach the death limit (TotalKills)
		decision.CanRespawn = (!config.Elimination || state.Deaths < config.TotalKills);
		decision.UseCheckpoint = false;
		decision.SpawnPos = ctx.GetSpawnPoint(player);
		decision.InvulnerableSecs = (float)config.SpawnInvulnerableSecs;
		return decision;
	}

	void BattleMode::OnCoinsCollected(IGameModeContext& ctx, Actors::Player* player, std::int32_t prevCount, std::int32_t newCount)
	{
		// Coins are not shared in competitive modes
	}

	void BattleMode::OnGemsCollected(IGameModeContext& ctx, Actors::Player* player, std::uint8_t gemType, std::int32_t prevCount, std::int32_t newCount)
	{
		// Gems are a plain collectible in Battle (no treasure scoring)
	}

	LevelExitAction BattleMode::OnLevelExitReached(IGameModeContext& ctx, Actors::Player* player)
	{
		// Battle has no level-exit win condition
		return LevelExitAction::Ignore;
	}

	GameEndResult BattleMode::CheckGameEnds(IGameModeContext& ctx)
	{
		const auto& config = ctx.GetServerConfiguration();
		auto players = ctx.GetPlayers();

		if (config.Elimination) {
			// Last player standing wins: a player is eliminated once their death count reaches the limit (TotalKills).
			// Count the players still in the round (spectators excluded) and remember one of them; the round ends
			// once at most one remains un-eliminated, and that survivor is the winner.
			std::int32_t aliveCount = 0;
			Actors::Player* lastAlive = nullptr;
			for (auto* player : players) {
				if (ctx.IsSpectating(player)) {
					continue;
				}
				if (ctx.GetPlayerState(player).Deaths < config.TotalKills) {
					aliveCount++;
					lastAlive = player;
				}
			}
			if (aliveCount <= 1) {
				return { true, false, lastAlive, (std::uint8_t)0 };
			}
			return GameEndResult::None();
		}

		// First player to reach the kill target wins
		std::uint32_t target = GetKillTarget(ctx);
		for (auto* player : players) {
			if (ctx.IsSpectating(player)) {
				continue;
			}
			if (ctx.GetPlayerState(player).Kills >= target) {
				return { true, false, player, (std::uint8_t)0 };
			}
		}
		return GameEndResult::None();
	}

	GameEndResult BattleMode::OnTimeOut(IGameModeContext& ctx)
	{
		// Overtime, the host decides by the score when it's off or over
		if (!ctx.GetServerConfiguration().Elimination && ctx.CanStartOvertime()) {
			return GameEndResult::Overtime();
		}
		return GameEndResult::None();
	}

	void BattleMode::OnOvertimeStarted(IGameModeContext& ctx)
	{
		// The cap is the best player or team score + 2, but never more than the max score
		std::uint32_t best = 0;
		if (IsTeamBased()) {
			std::uint8_t teamCount = ctx.GetTeamCount();
			for (std::uint8_t team = 0; team < teamCount; team++) {
				best = std::max<std::uint32_t>(best, ctx.GetTeamScore(team));
			}
		} else {
			for (auto* player : ctx.GetPlayers()) {
				if (!ctx.IsSpectating(player)) {
					best = std::max<std::uint32_t>(best, ctx.GetPlayerState(player).Kills);
				}
			}
		}
		std::uint32_t cap = best + 2;
		std::uint32_t maxScore = ctx.GetServerConfiguration().TotalKills;
		if (maxScore > 0) {
			cap = std::min<std::uint32_t>(cap, maxScore);
		}
		ctx.SetOvertimeScoreCap(cap);
	}

	std::uint32_t BattleMode::GetKillTarget(IGameModeContext& ctx)
	{
		std::uint32_t cap = ctx.GetOvertimeScoreCap();
		return (cap > 0 ? cap : ctx.GetServerConfiguration().TotalKills);
	}

	void BattleMode::AssignTeams(IGameModeContext& ctx)
	{
		// Free-for-all: every player is in their own team
		std::uint8_t team = 0;
		for (auto* player : ctx.GetPlayers()) {
			ctx.GetPlayerState(player).Team = team++;
		}
	}

	std::uint32_t BattleMode::GetRoundScore(IGameModeContext& ctx, Actors::Player* player)
	{
		return ctx.GetPlayerState(player).Kills;
	}

	void BattleMode::OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view)
	{
		auto& state = ctx.GetPlayerState(player);

		char stringBuffer[32];
		std::size_t length = formatInto(stringBuffer, "K: {}  D: {}  /{}", state.Kills, state.Deaths, GetKillTarget(ctx));
		// The cap of the overtime replaces the max score, in blue except in Team Battle
		Colorf color = (ctx.GetOvertimeScoreCap() > 0 && !IsTeamBased() ? Colorf(0.42f, 0.52f, 0.8f) : UI::Font::DefaultColor);
		hud.DrawHudText(GameModeFontType::Medium, { stringBuffer, length }, view.X + 14.0f, view.Y + 5.0f, 2.0f, UI::Alignment::TopLeft, color, 0.8f, 1.0f);

		hud.DrawPositionInRound(view, player);
	}
}

#endif
