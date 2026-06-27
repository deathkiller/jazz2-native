#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "IGameMode.h"

namespace Jazz2::Multiplayer::GameModes
{
	/**
		@brief Battle game mode (free-for-all)

		Every player fights for themselves. A kill increments the killer's score and the victim's death
		count; players respawn at a fresh spawn point with brief invulnerability. The round ends when a
		player reaches the kill target, or --- with elimination enabled --- when only one player is still
		within their death limit.
	*/
	class BattleMode : public IGameMode
	{
	public:
		MpGameMode GetMode() const override {
			return MpGameMode::Battle;
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
