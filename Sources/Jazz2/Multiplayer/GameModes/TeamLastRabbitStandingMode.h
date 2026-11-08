#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "CustomGameModeBase.h"
#include "../Teams.h"

#include <Containers/SmallVector.h>

namespace Jazz2::Multiplayer::GameModes
{
	/**
		@brief Team Last Rabbit Standing game mode (team-based)

		Every team has @ref ServerConfiguration::TeamLives lives. When the round starts, they're split as evenly as
		possible among at most @ref ServerConfiguration::MaxTeamPlayersWithLives players of the team (the later ones get the
		remainder), nobody more than @ref ServerConfiguration::PlayerLives, the rest goes to the reserve of the team and the
		other players are out. When a player runs out of lives, the reserve is split among the players of the team who are
		out, those out the longest first, as long as the team has room for players with lives. The last team with any
		lives wins.
	*/
	class TeamLastRabbitStandingMode : public CustomGameModeBase
	{
	public:
		TeamLastRabbitStandingMode();

		MpGameMode GetMode() const override {
			return MpGameMode::TeamLastRabbitStanding;
		}
		bool IsTeamBased() const override {
			return true;
		}

		void OnRoundStarted(IGameModeContext& ctx) override;
		void OnUpdate(IGameModeContext& ctx, float timeMult) override;
		void OnPlayerKilled(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* killer) override;
		RespawnDecision DecideRespawn(IGameModeContext& ctx, Actors::Player* player) override;
		GameEndResult CheckGameEnds(IGameModeContext& ctx) override;
		bool TryGetTeamScore(IGameModeContext& ctx, std::uint8_t team, std::uint32_t& score) override;
		std::uint32_t GetStateScore(const MpPlayerState& state) const override;
		void OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view) override;
		bool HasDamageAmplifierInOvertime() const override {
			return true;
		}

	protected:
		OvertimeRule GetOvertimeRule() const override {
			return OvertimeRule::TiedLeaders;
		}

	private:
		std::int32_t _reserve[MaxTeamCount];
		bool _teamPlaying[MaxTeamCount];
		SmallVector<std::uint32_t, 0> _participants;
		// Order in which players went out, the reserve goes to those out the longest first (ModeCarried holds it)
		std::uint32_t _outCounter;

		void TakeOut(IGameModeContext& ctx, Actors::Player* player);
		void DistributeReserve(IGameModeContext& ctx, std::uint8_t team);
		/** @brief Returns the share of the `index`-th of `count` players when `lives` are split, the later ones get the remainder */
		static std::int32_t GetShare(std::int32_t lives, std::int32_t index, std::int32_t count);
	};
}

#endif
