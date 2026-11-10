#include "CustomGameModeBase.h"

#if defined(WITH_MULTIPLAYER)

#include "../ServerInitialization.h"
#include "../Teams.h"
#include "../../Actors/Player.h"
#include "../../UI/Font.h"

#include "../../../nCine/I18n.h"

#include <Base/Format.h>

namespace Jazz2::Multiplayer::GameModes
{
	RespawnDecision CustomGameModeBase::DecideRespawn(IGameModeContext& ctx, Actors::Player* player)
	{
		RespawnDecision decision;
		decision.CanRespawn = true;
		decision.UseCheckpoint = false;
		decision.SpawnPos = ctx.GetSpawnPoint(player);
		decision.InvulnerableSecs = (float)ctx.GetServerConfiguration().SpawnInvulnerableSecs;
		return decision;
	}

	GameEndResult CustomGameModeBase::CheckGameEnds(IGameModeContext& ctx)
	{
		if (IsTeamBased()) {
			std::int32_t team = FindTeamAtTarget(ctx);
			if (team >= 0) {
				return GameEndResult::TeamWins((std::uint8_t)team);
			}
		} else if (auto* winner = FindPlayerAtTarget(ctx)) {
			return GameEndResult::PlayerWins(winner);
		}
		return GameEndResult::None();
	}

	void CustomGameModeBase::OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view)
	{
		// TRANSLATORS: Label of the score in the HUD of multiplayer game modes, followed by the score and the score to reach
		DrawScoreLine(ctx, hud, view, _("Score"), ctx.GetPlayerState(player).ModePoints, true);
		if (!IsTeamBased()) {
			hud.DrawPositionInRound(view, player);
		}
	}

	GameEndResult CustomGameModeBase::OnTimeOut(IGameModeContext& ctx)
	{
		return (ShouldStartOvertime(ctx) ? GameEndResult::Overtime() : DecideByScore(ctx));
	}

	bool CustomGameModeBase::ShouldStartOvertime(IGameModeContext& ctx)
	{
		if (!ctx.CanStartOvertime()) {
			return false;
		}
		switch (GetOvertimeRule()) {
			case OvertimeRule::ScoreCap:
			case OvertimeRule::NextLead:
				return true;
			case OvertimeRule::TiedLeaders: {
				bool tied;
				GetBestScore(ctx, tied);
				return tied;
			}
			default:
				return false;
		}
	}

	GameEndResult CustomGameModeBase::DecideByScore(IGameModeContext& ctx)
	{
		if (IsTeamBased()) {
			std::int32_t team = FindBestTeam(ctx);
			return (team >= 0 ? GameEndResult::TeamWins((std::uint8_t)team) : GameEndResult::Draw());
		}
		auto* winner = FindBestPlayer(ctx);
		return (winner != nullptr ? GameEndResult::PlayerWins(winner) : GameEndResult::Draw());
	}

	void CustomGameModeBase::OnOvertimeStarted(IGameModeContext& ctx)
	{
		OvertimeRule rule = GetOvertimeRule();
		if (rule != OvertimeRule::ScoreCap && rule != OvertimeRule::NextLead) {
			return;
		}

		// The cap is the current best score + 2, but never more than the max score
		bool tied;
		std::uint32_t best = GetBestScore(ctx, tied);
		std::uint32_t cap = best + (rule == OvertimeRule::ScoreCap ? 2 : 1);
		cap = std::min<std::uint32_t>(cap, ctx.GetServerConfiguration().TotalKills);
		ctx.SetOvertimeScoreCap(cap);
	}

	std::uint32_t CustomGameModeBase::GetScoreTarget(IGameModeContext& ctx)
	{
		std::uint32_t cap = ctx.GetOvertimeScoreCap();
		return (cap > 0 ? cap : ctx.GetServerConfiguration().TotalKills);
	}

	std::uint32_t CustomGameModeBase::GetBestScore(IGameModeContext& ctx, bool& tied)
	{
		std::uint32_t best = 0;
		bool any = false;
		tied = false;
		if (IsTeamBased()) {
			std::uint8_t teamCount = ctx.GetTeamCount();
			for (std::uint8_t team = 0; team < teamCount; team++) {
				std::uint32_t score = ctx.GetTeamScore(team);
				if (!any || score > best) {
					best = score;
					tied = false;
					any = true;
				} else if (score == best) {
					tied = true;
				}
			}
		} else {
			for (auto* player : ctx.GetPlayers()) {
				if (ctx.IsSpectating(player) && !IsEliminated(ctx, player)) {
					continue;
				}
				std::uint32_t score = GetLeaderScore(ctx, player);
				if (!any || score > best) {
					best = score;
					tied = false;
					any = true;
				} else if (score == best) {
					tied = true;
				}
			}
		}
		return best;
	}

	bool CustomGameModeBase::IsActive(IGameModeContext& ctx, Actors::Player* player)
	{
		return (!ctx.IsSpectating(player) && !IsEliminated(ctx, player));
	}

	bool CustomGameModeBase::HasFlag(IGameModeContext& ctx, Actors::Player* player, PlayerModeFlags flag)
	{
		return (ctx.GetPlayerState(player).ModeFlags & (std::uint8_t)flag) != 0;
	}

	bool CustomGameModeBase::SetFlag(IGameModeContext& ctx, Actors::Player* player, PlayerModeFlags flag, bool value)
	{
		auto& state = ctx.GetPlayerState(player);
		std::uint8_t prevFlags = state.ModeFlags;
		if (value) {
			state.ModeFlags |= (std::uint8_t)flag;
		} else {
			state.ModeFlags &= ~(std::uint8_t)flag;
		}
		return (prevFlags != state.ModeFlags);
	}

	void CustomGameModeBase::ClearFlag(IGameModeContext& ctx, std::uint32_t playerIndex, PlayerModeFlags flag)
	{
		if (auto* player = (playerIndex != UINT32_MAX ? ctx.FindPlayerByIndex(playerIndex) : nullptr)) {
			if (SetFlag(ctx, player, flag, false)) {
				ctx.SyncPlayerModeState(player);
			}
		}
	}

	void CustomGameModeBase::AddPoints(IGameModeContext& ctx, Actors::Player* player, std::int32_t points)
	{
		auto& state = ctx.GetPlayerState(player);
		std::int32_t newPoints = (std::int32_t)state.ModePoints + points;
		state.ModePoints = (std::uint32_t)(newPoints > 0 ? newPoints : 0);
	}

	Actors::Player* CustomGameModeBase::FindPlayerAtTarget(IGameModeContext& ctx)
	{
		std::uint32_t target = GetScoreTarget(ctx);
		for (auto* player : ctx.GetPlayers()) {
			if (!ctx.IsSpectating(player) && GetRoundScore(ctx, player) >= target) {
				return player;
			}
		}
		return nullptr;
	}

	std::int32_t CustomGameModeBase::FindTeamAtTarget(IGameModeContext& ctx)
	{
		std::uint32_t target = GetScoreTarget(ctx);
		std::uint8_t teamCount = ctx.GetTeamCount();
		for (std::uint8_t team = 0; team < teamCount; team++) {
			if (ctx.GetTeamScore(team) >= target) {
				return team;
			}
		}
		return -1;
	}

	Actors::Player* CustomGameModeBase::FindBestPlayer(IGameModeContext& ctx)
	{
		Actors::Player* best = nullptr;
		std::uint32_t bestScore = 0;
		bool tied = false;
		for (auto* player : ctx.GetPlayers()) {
			if (ctx.IsSpectating(player) && !IsEliminated(ctx, player)) {
				continue;
			}
			std::uint32_t score = GetRoundScore(ctx, player);
			if (best == nullptr || score > bestScore) {
				best = player;
				bestScore = score;
				tied = false;
			} else if (score == bestScore) {
				tied = true;
			}
		}
		return (tied ? nullptr : best);
	}

	std::int32_t CustomGameModeBase::FindBestTeam(IGameModeContext& ctx)
	{
		std::int32_t bestTeam = -1;
		std::uint32_t bestScore = 0;
		bool tied = false;
		std::uint8_t teamCount = ctx.GetTeamCount();
		for (std::uint8_t team = 0; team < teamCount; team++) {
			std::uint32_t score = ctx.GetTeamScore(team);
			if (bestTeam < 0 || score > bestScore) {
				bestTeam = team;
				bestScore = score;
				tied = false;
			} else if (score == bestScore) {
				tied = true;
			}
		}
		return (tied ? -1 : bestTeam);
	}

	void CustomGameModeBase::AddTeamBases(IGameModeContext& ctx, GameModeObjectType type, Vector2f* basePos)
	{
		std::uint8_t teamCount = ctx.GetTeamCount();
		for (std::uint8_t team = 0; team < teamCount; team++) {
			basePos[team] = ctx.GetTeamBasePosition(team);
			AddObject(ctx, type, team, basePos[team]);
		}
	}

	GameModeObject& CustomGameModeBase::AddObject(IGameModeContext& ctx, GameModeObjectType type, std::uint8_t team, Vector2f pos)
	{
		GameModeObject& obj = ctx.GetGameModeObjects().emplace_back();
		obj.Id = ctx.CreateGameModeObjectId();
		obj.Type = type;
		obj.Team = team;
		obj.CapturingTeam = GameModeObject::NeutralTeam;
		obj.Progress = 0;
		obj.Pos = pos;
		obj.CarrierPlayerIndex = UINT32_MAX;
		return obj;
	}

	void CustomGameModeBase::DrawScoreLine(IGameModeContext& ctx, IGameModeHUD& hud, const Rectf& view, StringView label, std::uint32_t value, bool withTarget)
	{
		char stringBuffer[64];
		std::size_t length = (withTarget
			? formatInto(stringBuffer, "{}: {}  /{}", label, value, GetScoreTarget(ctx))
			: formatInto(stringBuffer, "{}: {}", label, value));
		// The cap of the overtime is shown in blue
		Colorf color = (withTarget && ctx.GetOvertimeScoreCap() > 0 ? Colorf(0.42f, 0.52f, 0.8f) : UI::Font::DefaultColor);
		hud.DrawHudText(GameModeFontType::Medium, { stringBuffer, length }, view.X + 14.0f, view.Y + 5.0f, 2.0f,
			UI::Alignment::TopLeft, color, 0.8f, 1.0f);
	}

	void CustomGameModeBase::DrawUnderTeamScore(IGameModeContext& ctx, IGameModeHUD& hud, const Rectf& view, std::uint8_t team, StringView text)
	{
		// The same place and look as the flag state in Capture The Flag (see CaptureTheFlagMode::OnDrawHUD())
		std::uint8_t teamCount = ctx.GetTeamCount();
		if (teamCount < 2 || team >= teamCount) {
			return;
		}
		constexpr float Spacing = 54.0f;
		float x = view.X + view.W * 0.5f - Spacing * (teamCount - 1) * 0.5f + team * Spacing;
		hud.DrawHudText(GameModeFontType::Small, text, x, view.Y + 26.0f, 1.0f, UI::Alignment::Top, GetTeamColor(team), 0.7f, 0.88f);
	}

	void CustomGameModeBase::DrawStatusLine(IGameModeHUD& hud, const Rectf& view, StringView text, const Colorf& color)
	{
		hud.DrawHudText(GameModeFontType::Small, text, view.X + 14.0f, view.Y + 26.0f, 1.0f,
			UI::Alignment::TopLeft, color, 0.8f, 0.88f);
	}
}

#endif
