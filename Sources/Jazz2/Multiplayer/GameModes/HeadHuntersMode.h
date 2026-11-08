#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "CustomGameModeBase.h"
#include "../Teams.h"

namespace Jazz2::Multiplayer::GameModes
{
	/**
		@brief Head Hunters game mode (team-based)

		A roasted player drops all gems they carry plus one more. Gems brought to the base of the own team score
		for the team, at least 2 at once, `n` gems are worth `floor(n * ln(n))` points (2 gems 1 point, 3 gems
		3 points, 4 gems 5 points, 5 gems 8 points, ...). The first team to reach the score wins.
	*/
	class HeadHuntersMode : public CustomGameModeBase
	{
	public:
		HeadHuntersMode();

		MpGameMode GetMode() const override {
			return MpGameMode::HeadHunters;
		}
		bool IsTeamBased() const override {
			return true;
		}

		void OnRoundStarted(IGameModeContext& ctx) override;
		void OnUpdate(IGameModeContext& ctx, float timeMult) override;
		void OnPlayerKilled(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* killer) override;
		bool PreventsHazardDeath(IGameModeContext& ctx, Actors::Player* player) override;
		void OnGemsCollected(IGameModeContext& ctx, Actors::Player* player, std::uint8_t gemType, std::int32_t prevCount, std::int32_t newCount) override;
		bool TryGetTeamScore(IGameModeContext& ctx, std::uint8_t team, std::uint32_t& score) override;
		void OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view) override;
		StringView GetScoreboardLabel() const override;

		/** @brief Returns the points the specified number of gems is worth when delivered */
		static std::uint32_t GetPointsForGems(std::uint32_t gems);

	private:
		static constexpr float TouchRadius = 40.0f;
		static constexpr std::uint32_t MinGemsToDeliver = 2;

		Vector2f _basePos[MaxTeamCount];
		std::uint32_t _teamPoints[MaxTeamCount];
		Actors::Player* _pitVictim;
	};
}

#endif
