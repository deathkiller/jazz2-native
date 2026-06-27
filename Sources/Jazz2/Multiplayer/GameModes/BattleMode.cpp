#include "BattleMode.h"

#if defined(WITH_MULTIPLAYER)

#include "../ServerInitialization.h"
#include "../../Actors/Player.h"
#include "../../UI/Font.h"

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
			// Faithful replication of the original non-team elimination check in MpLevelHandler::CheckGameEnds.
			// eliminationCount counts players still within the death limit; eliminationWinner is the last eliminated
			// player. The original ends the round once at most one player remains un-eliminated.
			std::int32_t eliminationCount = 0;
			Actors::Player* eliminationWinner = nullptr;
			for (auto* player : players) {
				if (ctx.IsSpectating(player)) {
					continue;
				}
				if (ctx.GetPlayerState(player).Deaths < config.TotalKills) {
					eliminationCount++;
				} else {
					eliminationWinner = player;
				}
			}
			if (eliminationCount >= (std::int32_t)players.size() - 1) {
				return { true, false, eliminationWinner, (std::uint8_t)0 };
			}
			return GameEndResult::None();
		}

		// First player to reach the kill target wins
		for (auto* player : players) {
			if (ctx.IsSpectating(player)) {
				continue;
			}
			if (ctx.GetPlayerState(player).Kills >= config.TotalKills) {
				return { true, false, player, (std::uint8_t)0 };
			}
		}
		return GameEndResult::None();
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
		const auto& config = ctx.GetServerConfiguration();
		auto& state = ctx.GetPlayerState(player);

		char stringBuffer[32];
		std::size_t length = formatInto(stringBuffer, "K: {}  D: {}  /{}", state.Kills, state.Deaths, config.TotalKills);
		hud.DrawHudText(GameModeFontType::Medium, { stringBuffer, length }, view.X + 14.0f, view.Y + 5.0f, 2.0f, UI::Alignment::TopLeft, UI::Font::DefaultColor, 0.8f, 1.0f);

		hud.DrawPositionInRound(view, player);
	}
}

#endif
