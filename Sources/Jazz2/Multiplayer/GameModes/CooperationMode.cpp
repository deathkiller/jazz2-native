#include "CooperationMode.h"

#if defined(WITH_MULTIPLAYER)

#include "../../Actors/Player.h"
#include "../../PreferencesCache.h"
#include "../../UI/Font.h"

#include <Base/Format.h>

namespace Jazz2::Multiplayer
{
	void CooperationMode::OnRoundStarted(IGameModeContext& ctx)
	{
		// Cooperation has no per-round scoring to reset
	}

	void CooperationMode::OnUpdate(IGameModeContext& ctx, float timeMult)
	{
		// Nothing mode-specific to tick
	}

	void CooperationMode::OnPlayerSpawned(IGameModeContext& ctx, Actors::Player* player)
	{
		// Nothing mode-specific on spawn
	}

	void CooperationMode::OnPlayerKilled(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* killer)
	{
		// No kill/death scoring in cooperation
	}

	RespawnDecision CooperationMode::DecideRespawn(IGameModeContext& ctx, Actors::Player* player)
	{
		// Respawn at the player's own checkpoint without invulnerability, like classic co-op
		RespawnDecision decision;
		decision.CanRespawn = true;
		decision.UseCheckpoint = true;
		decision.SpawnPos = Vector2f::Zero;
		decision.InvulnerableSecs = 0.0f;
		return decision;
	}

	void CooperationMode::OnCoinsCollected(IGameModeContext& ctx, Actors::Player* player, std::int32_t prevCount, std::int32_t newCount)
	{
		// Coins are shared in cooperation: mirror the increment onto every other player
		if (prevCount < newCount) {
			std::int32_t increment = (newCount - prevCount);
			for (auto* other : ctx.GetPlayers()) {
				if (other != player) {
					other->AddCoinsInternal(increment);
				}
			}
		}
	}

	void CooperationMode::OnGemsCollected(IGameModeContext& ctx, Actors::Player* player, std::uint8_t gemType, std::int32_t prevCount, std::int32_t newCount)
	{
		// Gems are a plain collectible in cooperation (no treasure scoring)
	}

	LevelExitAction CooperationMode::OnLevelExitReached(IGameModeContext& ctx, Actors::Player* player)
	{
		return LevelExitAction::ChangeToNextLevel;
	}

	GameEndResult CooperationMode::CheckGameEnds(IGameModeContext& ctx)
	{
		// Cooperation never ends by score; it ends only by reaching the level exit or running out of lives
		return GameEndResult::None();
	}

	void CooperationMode::AssignTeams(IGameModeContext& ctx)
	{
		// Everyone is on a single team
		for (auto* player : ctx.GetPlayers()) {
			ctx.GetPlayerState(player).Team = 0;
		}
	}

	std::uint32_t CooperationMode::GetRoundScore(IGameModeContext& ctx, Actors::Player* player)
	{
		// Cooperation does not rank players
		return 0;
	}

	void CooperationMode::OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view)
	{
		// Cooperation shows the player's score (with a food icon in the reforged layout), like classic single player
		char stringBuffer[32];
		std::size_t length = formatInto(stringBuffer, "{:.8}", player->GetScore());

		if (PreferencesCache::EnableReforgedHUD) {
			hud.DrawHudIcon(GameModeHudIcon::Food, view.X + 3.0f, view.Y + 3.0f, 1.6f, UI::Alignment::TopLeft, Colorf::White, 1.0f, 1.0f);
			hud.DrawHudText(GameModeFontType::Small, { stringBuffer, length }, view.X + 14.0f, view.Y + 5.0f, 1.0f, UI::Alignment::TopLeft, UI::Font::DefaultColor, 1.0f, 0.88f);
		} else {
			hud.DrawHudText(GameModeFontType::Small, { stringBuffer, length }, view.X + 4.0f, view.Y + 1.0f, 1.0f, UI::Alignment::TopLeft, UI::Font::DefaultColor, 1.2f, 0.88f);
		}
	}
}

#endif
