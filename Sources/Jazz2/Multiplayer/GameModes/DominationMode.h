#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "CustomGameModeBase.h"
#include "../Teams.h"

#include <Containers/SmallVector.h>

namespace Jazz2::Multiplayer::GameModes
{
	/**
		@brief Domination game mode (team-based)

		The level has up to 16 control points --- events marked as easy-only or hard-only and CTF bases, otherwise a
		few spawn points. Touching a point for a second captures it for the team, unless a player of the team holding it
		touches it in the meantime, and points never become neutral again. Every point scores a point for its team
		once per interval, which is 5 seconds for every (started) 4 control points. The first team to reach the score
		wins.
	*/
	class DominationMode : public CustomGameModeBase
	{
	public:
		DominationMode();

		MpGameMode GetMode() const override {
			return MpGameMode::Domination;
		}
		bool IsTeamBased() const override {
			return true;
		}

		void OnRoundStarted(IGameModeContext& ctx) override;
		void OnUpdate(IGameModeContext& ctx, float timeMult) override;
		bool TryGetTeamScore(IGameModeContext& ctx, std::uint8_t team, std::uint32_t& score) override;
		std::uint32_t GetStateScore(const MpPlayerState& state) const override;
		void OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view) override;

	private:
		static constexpr std::uint32_t MaxControlPoints = 16;
		static constexpr float TouchRadius = 40.0f;
		static constexpr float CaptureTimeSecs = 1.0f;

		struct ControlPoint {
			std::uint32_t ObjectId;
			Vector2f Pos;
			std::uint8_t Team;
			std::uint8_t CapturingTeam;
			float CaptureFrames;
			float ScoreFrames;
			String Name;
		};

		SmallVector<ControlPoint, 0> _points;
		std::uint32_t _teamPoints[MaxTeamCount];
		float _scoreInterval;
	};
}

#endif
