#include "TreasureHuntMode.h"

#if defined(WITH_MULTIPLAYER)

#include "../ServerInitialization.h"
#include "../../UI/Font.h"

#include "../../../nCine/I18n.h"

#include <Base/Format.h>

namespace Jazz2::Multiplayer::GameModes
{
	void TreasureHuntMode::OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view)
	{
		const auto& config = ctx.GetServerConfiguration();
		auto& state = ctx.GetPlayerState(player);
		bool hasEnoughTreasure = (state.TreasureCollected >= config.TotalTreasureCollected);

		// Gem indicator: green once enough treasure is held, red otherwise
		GameModeHudIcon gemIcon = (hasEnoughTreasure ? GameModeHudIcon::GemGreen : GameModeHudIcon::GemRed);
		hud.DrawHudIcon(gemIcon, view.X + 8.0f, view.Y + 8.0f, 2.5f, UI::Alignment::TopLeft, Colorf(1.0f, 1.0f, 1.0f, 0.8f), 0.8f, 0.8f);

		Colorf textColor = (hasEnoughTreasure ? Colorf(0.34f, 0.5f, 0.38f, 0.5f) : UI::Font::DefaultColor);
		char stringBuffer[32];
		std::size_t length = formatInto(stringBuffer, "{}/{}", state.TreasureCollected, config.TotalTreasureCollected);
		hud.DrawHudText(GameModeFontType::Medium, { stringBuffer, length }, view.X + 38.0f, view.Y + 10.0f, 2.0f,
			UI::Alignment::TopLeft, textColor, 0.8f, 1.0f);

		if (hasEnoughTreasure) {
			// Animated prompt nudging the player to the exit
			auto findExitText = _("Find exit!");
			hud.DrawHudText(GameModeFontType::Small, findExitText, view.X + view.W * 0.5f, view.Y + 36.0f, 2.5f,
				UI::Alignment::Center, UI::Font::DefaultColor, 1.0f, 1.0f, 0.72f, 0.8f, 0.4f);
		}

		hud.DrawPositionInRound(view, player);
	}

	LevelExitAction TreasureHuntMode::OnLevelExitReached(IGameModeContext& ctx, Actors::Player* player)
	{
		// Reaching the exit only wins for a player holding enough treasure; bump them one ahead so they rank first,
		// then let the host end the round (see MpLevelHandler::BeginLevelChange).
		const auto& config = ctx.GetServerConfiguration();
		auto& state = ctx.GetPlayerState(player);
		if (state.TreasureCollected >= config.TotalTreasureCollected) {
			state.TreasureCollected++;
			return LevelExitAction::EndGameForInitiator;
		}
		return LevelExitAction::Ignore;
	}

	RespawnDecision TreasureHuntMode::DecideRespawn(IGameModeContext& ctx, Actors::Player* player)
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
