#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "IGameMode.h"

namespace Jazz2::Multiplayer
{
	/**
		@brief Cooperation game mode

		Classic shared-progress cooperative play: there is no win condition, coins are shared between all
		players, players respawn at their own checkpoint, and reaching the level exit advances everyone to
		the next level. The pre-game wait and countdown are skipped so the level starts immediately. The
		lives/game-over system (handled by the player and host) stays active, exactly like classic
		splitscreen co-op.
	*/
	class CooperationMode : public IGameMode
	{
	public:
		MpGameMode GetMode() const override {
			return MpGameMode::Cooperation;
		}
		bool IsTeamBased() const override {
			return false;
		}
		bool SkipsPreGameCountdown() const override {
			return true;
		}
		bool HasUnlimitedHealth() const override {
			return false;
		}
		bool LowerScoreWins() const override {
			return false;
		}

		void OnRoundStarted(IGameModeContext& ctx) override;
		void OnUpdate(IGameModeContext& ctx, float timeMult) override;
		void OnPlayerSpawned(IGameModeContext& ctx, Actors::Player* player) override;
		void OnPlayerKilled(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* killer) override;
		RespawnDecision DecideRespawn(IGameModeContext& ctx, Actors::Player* player) override;
		void OnCoinsCollected(IGameModeContext& ctx, Actors::Player* player, std::int32_t prevCount, std::int32_t newCount) override;
		void OnGemsCollected(IGameModeContext& ctx, Actors::Player* player, std::uint8_t gemType, std::int32_t prevCount, std::int32_t newCount) override;
		LevelExitAction OnLevelExitReached(IGameModeContext& ctx, Actors::Player* player) override;
		GameEndResult CheckGameEnds(IGameModeContext& ctx) override;
		void AssignTeams(IGameModeContext& ctx) override;
		std::uint32_t GetRoundScore(IGameModeContext& ctx, Actors::Player* player) override;
		void OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view) override;
	};
}

#endif
