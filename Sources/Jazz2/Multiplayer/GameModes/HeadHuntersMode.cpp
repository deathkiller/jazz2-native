#include "HeadHuntersMode.h"

#if defined(WITH_MULTIPLAYER)

#include "../ServerInitialization.h"
#include "../../Actors/Player.h"
#include "../../UI/Font.h"
#include "../../../nCine/I18n.h"

#include <cmath>
#include <Base/Format.h>
#include <cstring>

namespace Jazz2::Multiplayer::GameModes
{
	HeadHuntersMode::HeadHuntersMode()
		: _teamPoints{}, _pitVictim(nullptr)
	{
	}

	void HeadHuntersMode::OnRoundStarted(IGameModeContext& ctx)
	{
		std::memset(_teamPoints, 0, sizeof(_teamPoints));

		ctx.GetGameModeObjects().clear();
		AddTeamBases(ctx, GameModeObjectType::Base, _basePos);
		ctx.SyncGameModeObjects();
		ctx.SyncTeamScores();
	}

	void HeadHuntersMode::OnUpdate(IGameModeContext& ctx, float timeMult)
	{
		std::uint8_t teamCount = ctx.GetTeamCount();
		bool scoreChanged = false;

		const auto& config = ctx.GetServerConfiguration();
		for (auto* player : ctx.GetPlayers()) {
			auto& state = ctx.GetPlayerState(player);
			// Passing, an injured player gives the gems to a nearby teammate with full health
			if (config.AllowPassing && state.ModeCarried > 0 && !ctx.IsSpectating(player) && player->GetHealth() > 0 &&
				player->GetHealth() < player->GetMaxHealth()) {
				for (auto* mate : ctx.GetPlayers()) {
					if (mate != player && !ctx.IsSpectating(mate) && ctx.GetPlayerState(mate).Team == state.Team &&
						mate->GetHealth() > 0 && mate->GetHealth() >= mate->GetMaxHealth() && (mate->GetPos() - player->GetPos()).Length() <= TouchRadius) {
						std::int32_t gems = ctx.GetPlayerGemCount(player);
						ctx.ClearPlayerGems(player);
						state.ModeCarried = 0;
						ctx.SyncPlayerModeState(player);
						ctx.AddPlayerGems(mate, gems);
						break;
					}
				}
			}

			if (ctx.IsSpectating(player) || player->GetHealth() <= 0 || state.Team >= teamCount ||
				state.ModeCarried < MinGemsToDeliver || (player->GetPos() - _basePos[state.Team]).Length() > TouchRadius) {
				continue;
			}

			std::uint32_t gems = state.ModeCarried;
			std::uint32_t points = GetPointsForGems(gems);
			_teamPoints[state.Team] += points;
			AddPoints(ctx, player, (std::int32_t)points);
			ctx.ClearPlayerGems(player);
			state.ModeCarried = 0;
			ctx.SyncPlayerModeState(player);
			scoreChanged = true;

			ctx.ShowAlertToAllPlayers(ServerMessage(ServerMessageType::HeadHuntersDelivered, ctx.GetPlayerName(player), gems, points));
		}

		if (scoreChanged) {
			ctx.SyncTeamScores();
			ctx.CheckGameEnds();
		}
	}

	bool HeadHuntersMode::PreventsHazardDeath(IGameModeContext& ctx, Actors::Player* player)
	{
		// A team without points can't lose one, the player is reset to 1 health instead
		_pitVictim = nullptr;
		std::uint8_t team = ctx.GetPlayerState(player).Team;
		if (ctx.GetPlayerGemCount(player) <= 0 || team >= MaxTeamCount) {
			return false;
		}
		if (_teamPoints[team] == 0) {
			return true;
		}
		_pitVictim = player;
		return false;
	}

	void HeadHuntersMode::OnPlayerKilled(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* killer)
	{
		const auto& config = ctx.GetServerConfiguration();
		auto& state = ctx.GetPlayerState(victim);
		std::int32_t carried = ctx.GetPlayerGemCount(victim);

		// A self-inflicted pit death with gems costs the team a point
		if (victim == _pitVictim && carried > 0 && state.Team < MaxTeamCount && _teamPoints[state.Team] > 0) {
			_teamPoints[state.Team]--;
			ctx.SyncTeamScores();
		}
		_pitVictim = nullptr;

		// All gems the player carried, plus one for the head itself (ExtraDroppedGems), the player can't pick them up again
		std::int32_t count = carried + (std::int32_t)config.ExtraDroppedGems;
		ctx.ClearPlayerGems(victim);
		if (config.TransferDroppedGems && killer != nullptr && killer != victim) {
			ctx.AddPlayerGems(killer, count);
		} else {
			ctx.SpawnGems(victim->GetPos(), count, victim);
		}

		state.ModeCarried = 0;
		ctx.SyncPlayerModeState(victim);
	}
	void HeadHuntersMode::OnGemsCollected(IGameModeContext& ctx, Actors::Player* player, std::uint8_t gemType, std::int32_t prevCount, std::int32_t newCount)
	{
		auto& state = ctx.GetPlayerState(player);
		std::uint32_t carried = (std::uint32_t)std::max<std::int32_t>(ctx.GetPlayerGemCount(player), 0);
		if (state.ModeCarried != carried) {
			state.ModeCarried = carried;
			ctx.SyncPlayerModeState(player);
		}
	}

	bool HeadHuntersMode::TryGetTeamScore(IGameModeContext& ctx, std::uint8_t team, std::uint32_t& score)
	{
		score = (team < MaxTeamCount ? _teamPoints[team] : 0);
		return true;
	}

	void HeadHuntersMode::OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view)
	{
		// The gems the player carries in pink and the points they're worth in silver
		auto& state = ctx.GetPlayerState(player);
		char buffer[32];
		std::size_t length = formatInto(buffer, "{}", state.ModeCarried);
		hud.DrawHudText(GameModeFontType::Medium, { buffer, length }, view.X + 14.0f, view.Y + 5.0f, 2.0f,
			UI::Alignment::TopLeft, Colorf(0.86f, 0.46f, 0.72f), 0.8f, 1.0f);
		std::uint32_t points = GetPointsForGems(state.ModeCarried);
		if (points > 0) {
			length = formatInto(buffer, "{}", points);
			hud.DrawHudText(GameModeFontType::Medium, { buffer, length }, view.X + 54.0f, view.Y + 5.0f, 2.0f,
				UI::Alignment::TopLeft, Colorf(0.72f, 0.74f, 0.78f), 0.8f, 1.0f);
		}
	}
	StringView HeadHuntersMode::GetScoreboardLabel() const
	{
		return _("Points");
	}

	std::uint32_t HeadHuntersMode::GetPointsForGems(std::uint32_t gems)
	{
		if (gems < MinGemsToDeliver) {
			return 0;
		}
		return (std::uint32_t)std::floor(gems * std::log((float)gems));
	}
}

#endif
