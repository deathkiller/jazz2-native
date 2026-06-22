#include "RaceMode.h"

#if defined(WITH_MULTIPLAYER)

#include "../ServerInitialization.h"
#include "../../UI/Font.h"

#include <Base/Format.h>
#include <algorithm>
#include <cmath>

namespace Jazz2::Multiplayer
{
	void RaceMode::OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view)
	{
		const auto& config = ctx.GetServerConfiguration();
		auto& state = ctx.GetPlayerState(player);

		char stringBuffer[32];

		// Lap counter (top-right)
		std::size_t length = formatInto(stringBuffer, "{}/{}", state.Laps + 1, config.TotalLaps);
		hud.DrawHudText(GameModeFontType::Medium, { stringBuffer, length }, view.X + 65.0f, view.Y + 7.0f, 2.0f,
			UI::Alignment::TopRight, UI::Font::DefaultColor, 0.88f, 1.0f);

		// Current lap time (top-left, next to the lap counter)
		float sinceLapStarted = state.LapStarted.secondsSince();
		std::int32_t minutes = std::max(0, (std::int32_t)(sinceLapStarted / 60));
		std::int32_t seconds = std::max(0, (std::int32_t)fmod(sinceLapStarted, 60));
		std::int32_t milliseconds = std::max(0, (std::int32_t)(fmod(sinceLapStarted, 1) * 100));
		length = formatInto(stringBuffer, "{}:{:.2}:{:.2}", minutes, seconds, milliseconds);
		hud.DrawHudText(GameModeFontType::Medium, { stringBuffer, length }, view.X + 14.0f + 80.0f, view.Y + 10.0f, 1.4f,
			UI::Alignment::TopLeft, UI::Font::DefaultColor, 0.7f, 1.0f);

		hud.DrawMinimap(view, player);
		hud.DrawPositionInRound(view, player);
	}

	RespawnDecision RaceMode::DecideRespawn(IGameModeContext& ctx, Actors::Player* player)
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
