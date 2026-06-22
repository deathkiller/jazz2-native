#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "IGameMode.h"

namespace Jazz2::Multiplayer
{
	/**
		@brief Capture The Flag game mode

		Teams steal each other's flags and return them to their base to score. This mode owns its HUD (each team's
		flag status --- home / taken / dropped --- shown under the team-score header) through @ref OnDrawHUD. Its
		round logic --- flag bases, carrying/dropping/capturing and the capture win condition --- is still handled
		by the host for now, so @ref OwnsRoundLogic returns `false` and the gameplay hooks below are not yet invoked.
	*/
	class CaptureTheFlagMode : public IGameMode
	{
	public:
		MpGameMode GetMode() const override {
			return MpGameMode::CaptureTheFlag;
		}
		bool IsTeamBased() const override {
			return true;
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

		// The win condition (CheckGameEnds) and the flag-handling logic stay host-driven while OwnsRoundLogic() == false;
		// the respawn, level-exit and HUD hooks below are owned by this mode (invoked regardless of that flag).
		void OnRoundStarted(IGameModeContext&) override { }
		void OnUpdate(IGameModeContext&, float) override { }
		void OnPlayerSpawned(IGameModeContext&, Actors::Player*) override { }
		void OnPlayerKilled(IGameModeContext&, Actors::Player*, Actors::Player*) override { }
		RespawnDecision DecideRespawn(IGameModeContext& ctx, Actors::Player* player) override;
		void OnCoinsCollected(IGameModeContext&, Actors::Player*, std::int32_t, std::int32_t) override { }
		void OnGemsCollected(IGameModeContext&, Actors::Player*, std::uint8_t, std::int32_t, std::int32_t) override { }
		LevelExitAction OnLevelExitReached(IGameModeContext&, Actors::Player*) override {
			return LevelExitAction::Ignore;
		}
		GameEndResult CheckGameEnds(IGameModeContext&) override {
			return GameEndResult::None();
		}
		void AssignTeams(IGameModeContext&) override { }
		std::uint32_t GetRoundScore(IGameModeContext&, Actors::Player*) override {
			return 0;
		}

		void OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view) override;
	};
}

#endif
