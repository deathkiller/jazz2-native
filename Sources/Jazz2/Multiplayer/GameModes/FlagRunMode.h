#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "CustomGameModeBase.h"
#include "../Teams.h"

#include <Containers/SmallVector.h>

namespace Jazz2::Multiplayer::GameModes
{
	/**
		@brief Flag Run game mode (team-based)

		While nobody carries the flag, it can be captured at any capture point (an event marked as easy-only or hard-only,
		otherwise a spawn point). The carrier has 45 seconds to take it into an enemy base, or self-destructs and the flag
		goes back. A delivery scores 3 points with more than 30 seconds left, 2 with more than 15 and 1 otherwise, plus
		one for every roast of the carrier (at most 3). Roasting a carrier with 15 seconds or less left steals the flag
		with 30 seconds on the clock and one bonus point, otherwise the flag goes back. The first team to reach the
		score wins.
	*/
	class FlagRunMode : public CustomGameModeBase
	{
	public:
		FlagRunMode();

		MpGameMode GetMode() const override {
			return MpGameMode::FlagRun;
		}
		bool IsTeamBased() const override {
			return true;
		}

		void OnRoundStarted(IGameModeContext& ctx) override;
		void OnUpdate(IGameModeContext& ctx, float timeMult) override;
		void OnPlayerKilled(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* killer) override;
		bool TryGetTeamScore(IGameModeContext& ctx, std::uint8_t team, std::uint32_t& score) override;
		void OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view) override;
		StringView GetScoreboardLabel() const override;
		StringView GetRoleLabel(std::uint8_t modeFlags) const override;

	private:
		static constexpr float TouchRadius = 40.0f;
		static constexpr float CaptureTimeSecs = 45.0f;
		static constexpr float StealTimeSecs = 30.0f;
		static constexpr float StealWindowSecs = 16.0f;
		static constexpr std::uint32_t MaxExtraPoints = 3;

		SmallVector<Vector2f, 0> _capturePoints;
		Vector2f _basePos[MaxTeamCount];
		std::uint32_t _carrierIndex;
		float _timeLeft;
		std::uint32_t _extraPoints;
		std::uint32_t _teamPoints[MaxTeamCount];
		SmallVector<Pair<std::uint32_t, std::uint32_t>, 0> _captures;

		void PickUp(IGameModeContext& ctx, Actors::Player* player, float timeSecs, std::uint32_t extraPoints, Vector2f pos);
		void ReturnFlag(IGameModeContext& ctx);
		void RebuildFlags(IGameModeContext& ctx, Vector2f carrierPos, std::uint8_t carrierTeam);
		std::uint32_t GetCaptures(Actors::Player* player) const;
		void AddCapture(Actors::Player* player);
	};
}

#endif
