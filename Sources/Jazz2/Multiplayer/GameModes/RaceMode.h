#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "IGameMode.h"

namespace Jazz2::Multiplayer::GameModes
{
	/**
		@brief Race game mode

		Players race to complete a number of laps. This mode owns its HUD (lap counter, lap timer, track minimap and
		standings) through @ref OnDrawHUD. Its round logic --- lap counting, overtime and the win condition --- is
		still handled by the host for now, so @ref OwnsRoundLogic returns `false` and the gameplay hooks below are
		not yet invoked (they keep the interface satisfied until the logic is ported in a later phase).
	*/
	class RaceMode : public IGameMode
	{
	public:
		MpGameMode GetMode() const override {
			return MpGameMode::Race;
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
			return true;
		}
		bool OwnsRoundLogic() const override {
			return false;
		}

		// The win condition (CheckGameEnds) and per-tick race logic stay host-driven while OwnsRoundLogic() == false;
		// the respawn, level-exit, score and HUD hooks below are owned by this mode (invoked regardless of that flag).
		void OnRoundStarted(IGameModeContext&) override { }
		void OnUpdate(IGameModeContext&, float) override { }
		void OnPlayerSpawned(IGameModeContext&, Actors::Player*) override { }
		void OnPlayerKilled(IGameModeContext&, Actors::Player*, Actors::Player*) override { }
		RespawnDecision DecideRespawn(IGameModeContext& ctx, Actors::Player* player) override;
		void OnCoinsCollected(IGameModeContext&, Actors::Player*, std::int32_t, std::int32_t) override { }
		void OnGemsCollected(IGameModeContext&, Actors::Player*, std::uint8_t, std::int32_t, std::int32_t) override { }
		LevelExitAction OnLevelExitReached(IGameModeContext&, Actors::Player*) override {
			return LevelExitAction::WarpToStartIncrementLap;
		}
		GameEndResult CheckGameEnds(IGameModeContext&) override {
			return GameEndResult::None();
		}
		void AssignTeams(IGameModeContext&) override { }
		std::uint32_t GetRoundScore(IGameModeContext& ctx, Actors::Player* player) override {
			return ctx.GetPlayerState(player).Laps;
		}

		void OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view) override;
	};
}

#endif
