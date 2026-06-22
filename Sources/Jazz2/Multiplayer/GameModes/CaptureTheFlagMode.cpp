#include "CaptureTheFlagMode.h"

#if defined(WITH_MULTIPLAYER)

#include "../ServerInitialization.h"
#include "../Teams.h"

#include "../../../nCine/I18n.h"

namespace Jazz2::Multiplayer
{
	void CaptureTheFlagMode::OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view)
	{
		// The per-team capture counts are drawn by the generic team-score header; here we show each team's flag
		// status (home / taken / dropped) centered just below it, aligned under each team's count.
		const StringView flagStateText[] = { _("home"), _("taken"), _("dropped") };
		std::uint8_t teamCount = ctx.GetCtfFlagStateCount();
		if (teamCount >= 2) {
			float spacing = 54.0f;
			float startX = view.X + view.W * 0.5f - spacing * (teamCount - 1) * 0.5f;
			for (std::uint8_t team = 0; team < teamCount; team++) {
				float x = startX + team * spacing;
				std::uint8_t state = ctx.GetCtfFlagState(team);
				StringView label = flagStateText[state < 3 ? state : 0];
				hud.DrawHudText(GameModeFontType::Small, label, x, view.Y + 26.0f, 1.0f, UI::Alignment::Top,
					GetTeamColor(team), 0.7f, 0.88f);
			}
		}
	}

	RespawnDecision CaptureTheFlagMode::DecideRespawn(IGameModeContext& ctx, Actors::Player* player)
	{
		// Competitive modes respawn at a fresh spawn point with brief invulnerability (no checkpoints)
		RespawnDecision decision;
		decision.CanRespawn = true;
		decision.UseCheckpoint = false;
		decision.SpawnPos = ctx.GetSpawnPoint(player);
		decision.InvulnerableSecs = (float)ctx.GetServerConfiguration().SpawnInvulnerableSecs;
		return decision;
	}
}

#endif
