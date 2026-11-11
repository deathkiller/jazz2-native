#include "LastRabbitStandingMode.h"

#if defined(WITH_MULTIPLAYER)

#include "../ServerInitialization.h"
#include "../../Actors/Player.h"
#include "../../UI/Font.h"
#include "../../../nCine/I18n.h"
#include "../../../nCine/Base/Algorithms.h"
#include "../../../nCine/Base/FrameTimer.h"

#include <float.h>
#include <Base/Format.h>
#include <Containers/SmallVector.h>

namespace Jazz2::Multiplayer::GameModes
{
	LastRabbitStandingMode::LastRabbitStandingMode()
		: _participantCount(0)
	{
	}

	void LastRabbitStandingMode::OnRoundStarted(IGameModeContext& ctx)
	{
		const auto& config = ctx.GetServerConfiguration();
		_participantCount = 0;
		for (auto* player : ctx.GetPlayers()) {
			if (ctx.IsSpectating(player)) {
				continue;
			}
			auto& state = ctx.GetPlayerState(player);
			state.ModeLives = (std::int32_t)config.PlayerLives;
			state.ModeTimer = 0.0f;
			ctx.SyncPlayerModeState(player);
			_participantCount++;
		}
	}

	void LastRabbitStandingMode::OnUpdate(IGameModeContext& ctx, float timeMult)
	{
		const auto& config = ctx.GetServerConfiguration();
		for (auto* player : ctx.GetPlayers()) {
			if (ctx.IsSpectating(player) || IsEliminated(ctx, player)) {
				continue;
			}
			auto& state = ctx.GetPlayerState(player);
			if (state.ModeLives <= 0) {
				// A late joiner, out unless late joiners are allowed and nobody placed 4th or 3rd yet
				std::int32_t standingCount;
				FindLastStanding(ctx, standingCount);
				if (config.LateJoinersCanPlay && standingCount > 3) {
					state.ModeLives = (std::int32_t)config.PlayerLives;
					state.ModeTimer = 0.0f;
					ctx.SyncPlayerModeState(player);
				} else {
					// Never placed, ranked below everybody who was out during the round
					state.DeathElapsedFrames = 0.0f;
					OnLateJoinerOut(ctx, player);
					Eliminate(ctx, player);
					ctx.SetPlayerOut(player, true);
				}
				continue;
			}

			// Anti-camping, a player who didn't roast anybody for 2 minutes has the position announced
			if (player->GetHealth() > 0) {
				state.ModeTimer += timeMult;
				if (state.ModeTimer >= CampingLimitSecs * FrameTimer::FramesPerSecond) {
					state.ModeTimer = 0.0f;
					AnnouncePosition(ctx, player);
				}
			}
		}
	}

	void LastRabbitStandingMode::OnPlayerKilled(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* killer)
	{
		if (killer != nullptr && killer != victim) {
			ctx.GetPlayerState(killer).ModeTimer = 0.0f;
		}

		auto& state = ctx.GetPlayerState(victim);
		if (state.ModeLives > 0) {
			state.ModeLives--;
		}
		if (state.ModeLives <= 0) {
			// The host moves the player to spectate mode once the death animation ends
			Eliminate(ctx, victim);
		} else {
			ctx.SyncPlayerModeState(victim);
		}
	}

	RespawnDecision LastRabbitStandingMode::DecideRespawn(IGameModeContext& ctx, Actors::Player* player)
	{
		RespawnDecision decision = CustomGameModeBase::DecideRespawn(ctx, player);
		decision.CanRespawn = (ctx.GetPlayerState(player).ModeLives > 0);
		return decision;
	}

	GameEndResult LastRabbitStandingMode::CheckGameEnds(IGameModeContext& ctx)
	{
		if (_participantCount < 2) {
			return GameEndResult::None();
		}
		std::int32_t standingCount;
		auto* lastStanding = FindLastStanding(ctx, standingCount);
		return (standingCount <= 1 ? GameEndResult::PlayerWins(lastStanding) : GameEndResult::None());
	}

	std::uint32_t LastRabbitStandingMode::GetStateScore(const MpPlayerState& state) const
	{
		return (std::uint32_t)std::max<std::int32_t>(state.ModeLives, 0);
	}

	StringView LastRabbitStandingMode::GetScoreboardLabel() const
	{
		// TRANSLATORS: Column header in the multiplayer scoreboard (lives left in Last Rabbit Standing modes), keep it short
		return _("Lives");
	}

	void LastRabbitStandingMode::OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view)
	{
		// TRANSLATORS: Label in the HUD of Last Rabbit Standing modes, followed by the number of lives left
		DrawScoreLine(ctx, hud, view, _("Lives"), (std::uint32_t)std::max<std::int32_t>(ctx.GetPlayerState(player).ModeLives, 0), false);
		hud.DrawPositionInRound(view, player);
	}

	void LastRabbitStandingMode::Eliminate(IGameModeContext& ctx, Actors::Player* player)
	{
		if (SetFlag(ctx, player, PlayerModeFlags::Eliminated, true)) {
			auto& state = ctx.GetPlayerState(player);
			state.ModeLives = 0;
			if (state.DeathElapsedFrames == FLT_MAX) {
				state.DeathElapsedFrames = ctx.GetElapsedFrames();
			}
			ctx.SyncPlayerModeState(player);
			ctx.ShowAlertToAllPlayers(ServerMessage(ServerMessageType::PlayerIsOut, ctx.GetPlayerName(player)));
		}
	}

	Actors::Player* LastRabbitStandingMode::FindLastStanding(IGameModeContext& ctx, std::int32_t& standingCount)
	{
		Actors::Player* lastStanding = nullptr;
		standingCount = 0;
		for (auto* player : ctx.GetPlayers()) {
			if (IsActive(ctx, player) && ctx.GetPlayerState(player).ModeLives > 0) {
				standingCount++;
				lastStanding = player;
			}
		}
		return lastStanding;
	}

	void LastRabbitStandingMode::AnnouncePosition(IGameModeContext& ctx, Actors::Player* player)
	{
		Vector2f pos = player->GetPos();
		ctx.SendMessageToAllPlayers(ServerMessage(ServerMessageType::PlayerPosition, ctx.GetPlayerName(player),
			(std::int32_t)(pos.X / 32.0f), (std::int32_t)(pos.Y / 32.0f)));
	}

	ExtendedLastRabbitStandingMode::ExtendedLastRabbitStandingMode()
		: _bonusApplied(false)
	{
	}

	void ExtendedLastRabbitStandingMode::OnRoundStarted(IGameModeContext& ctx)
	{
		LastRabbitStandingMode::OnRoundStarted(ctx);
		_bonusApplied = false;
	}

	void ExtendedLastRabbitStandingMode::OnPlayerKilled(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* killer)
	{
		if (killer != nullptr && killer != victim && !_bonusApplied) {
			AddPoints(ctx, killer, 1);
			ctx.SyncPlayerModeState(killer);
		}
		LastRabbitStandingMode::OnPlayerKilled(ctx, victim, killer);
	}

	GameEndResult ExtendedLastRabbitStandingMode::CheckGameEnds(IGameModeContext& ctx)
	{
		GameEndResult result = LastRabbitStandingMode::CheckGameEnds(ctx);
		if (!result.ShouldEnd) {
			return result;
		}

		ApplyBonus(ctx);
		auto* winner = FindBestPlayer(ctx);
		return GameEndResult::PlayerWins(winner != nullptr ? winner : result.Winner);
	}

	GameEndResult ExtendedLastRabbitStandingMode::OnTimeOut(IGameModeContext& ctx)
	{
		// Overtime only if several players have the most lives
		if (ShouldStartOvertime(ctx)) {
			return GameEndResult::Overtime();
		}
		ApplyBonus(ctx);
		return DecideByScore(ctx);
	}

	void ExtendedLastRabbitStandingMode::ApplyBonus(IGameModeContext& ctx)
	{
		if (_bonusApplied) {
			return;
		}
		_bonusApplied = true;

		// The last three players to stay alive get their points multiplied, those still in by their lives first
		SmallVector<Actors::Player*, 0> ranked;
		for (auto* player : ctx.GetPlayers()) {
			if (!ctx.IsSpectating(player) || IsEliminated(ctx, player)) {
				ranked.push_back(player);
			}
		}
		nCine::sort(ranked.begin(), ranked.end(), [&ctx](Actors::Player* a, Actors::Player* b) {
			auto& sa = ctx.GetPlayerState(a);
			auto& sb = ctx.GetPlayerState(b);
			if (sa.ModeLives != sb.ModeLives) {
				return (sa.ModeLives > sb.ModeLives);
			}
			return (sa.DeathElapsedFrames > sb.DeathElapsedFrames);
		});

		static constexpr float Multipliers[] = { 1.5f, 1.25f, 1.125f };
		for (std::size_t i = 0; i < ranked.size() && i < 3; i++) {
			auto& state = ctx.GetPlayerState(ranked[i]);
			state.ModePoints = (std::uint32_t)(state.ModePoints * Multipliers[i]);
			ctx.SyncPlayerModeState(ranked[i]);
		}
	}

	void ExtendedLastRabbitStandingMode::OnLateJoinerOut(IGameModeContext& ctx, Actors::Player* player)
	{
		ctx.GetPlayerState(player).ModePoints = 0;
	}
	std::uint32_t ExtendedLastRabbitStandingMode::GetStateScore(const MpPlayerState& state) const
	{
		return state.ModePoints;
	}

	void ExtendedLastRabbitStandingMode::OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view)
	{
		auto& state = ctx.GetPlayerState(player);
		// TRANSLATORS: Score line in the HUD of Extended Last Rabbit Standing, first {} is the number of lives left, second {} the points
		String text = _f("Lives: {}  Points: {}", std::max<std::int32_t>(state.ModeLives, 0), state.ModePoints);
		hud.DrawHudText(GameModeFontType::Medium, text, view.X + 14.0f, view.Y + 5.0f, 2.0f,
			UI::Alignment::TopLeft, UI::Font::DefaultColor, 0.8f, 1.0f);
		hud.DrawPositionInRound(view, player);
	}

	StringView ExtendedLastRabbitStandingMode::GetScoreboardLabel() const
	{
		return _("Points");
	}
}

#endif
