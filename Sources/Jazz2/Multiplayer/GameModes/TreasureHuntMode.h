#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "IGameMode.h"

namespace Jazz2::Multiplayer::GameModes
{
	/**
		@brief Treasure Hunt game mode

		Players collect gems (weighted into a treasure total) and win by reaching the level exit once they hold
		enough treasure. This mode owns its HUD (treasure counter, a red/green gem indicator and a "Find exit!"
		prompt) through @ref OnDrawHUD. Its round logic --- gem weighting, the treasure-loss-on-damage and the
		exit win condition --- is still handled by the host for now, so @ref OwnsRoundLogic returns `false` and the
		gameplay hooks below are not yet invoked.
	*/
	class TreasureHuntMode : public IGameMode
	{
	public:
		MpGameMode GetMode() const override {
			return MpGameMode::TreasureHunt;
		}
		bool IsTeamBased() const override {
			return false;
		}
		bool SkipsPreGameCountdown() const override {
			return false;
		}
		bool HasUnlimitedHealth() const override {
			return false;
		}
		bool LowerScoreWins() const override {
			return false;
		}
		bool OwnsRoundLogic() const override {
			return false;
		}

		// The win condition (CheckGameEnds) and per-tick treasure logic stay host-driven while OwnsRoundLogic() == false;
		// the respawn, level-exit, score and HUD hooks below are owned by this mode (invoked regardless of that flag).
		void OnRoundStarted(IGameModeContext&) override { }
		void OnUpdate(IGameModeContext&, float) override { }
		void OnPlayerSpawned(IGameModeContext&, Actors::Player*) override { }
		void OnPlayerKilled(IGameModeContext&, Actors::Player*, Actors::Player*) override { }
		RespawnDecision DecideRespawn(IGameModeContext& ctx, Actors::Player* player) override;
		void OnCoinsCollected(IGameModeContext&, Actors::Player*, std::int32_t, std::int32_t) override { }
		void OnGemsCollected(IGameModeContext&, Actors::Player*, std::uint8_t, std::int32_t, std::int32_t) override { }
		LevelExitAction OnLevelExitReached(IGameModeContext& ctx, Actors::Player* player) override;
		GameEndResult CheckGameEnds(IGameModeContext&) override {
			return GameEndResult::None();
		}
		void AssignTeams(IGameModeContext&) override { }
		std::uint32_t GetRoundScore(IGameModeContext& ctx, Actors::Player* player) override {
			return ctx.GetPlayerState(player).TreasureCollected;
		}

		void OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view) override;
	};
}

#endif
