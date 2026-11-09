#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "CustomGameModeBase.h"

namespace Jazz2::Multiplayer::GameModes
{
	/**
		@brief Last Rabbit Standing game mode (free-for-all)

		Every player starts the round with @ref ServerConfiguration::PlayerLives lives and each death costs one. A player
		without lives is out and watches the rest of the round, so does anybody who joins after it started. The last
		player standing wins, roasts score nothing. When the time runs out, the player with the most lives wins.
	*/
	class LastRabbitStandingMode : public CustomGameModeBase
	{
	public:
		LastRabbitStandingMode();

		MpGameMode GetMode() const override {
			return MpGameMode::LastRabbitStanding;
		}
		bool IsTeamBased() const override {
			return false;
		}

		void OnRoundStarted(IGameModeContext& ctx) override;
		void OnUpdate(IGameModeContext& ctx, float timeMult) override;
		void OnPlayerKilled(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* killer) override;
		RespawnDecision DecideRespawn(IGameModeContext& ctx, Actors::Player* player) override;
		GameEndResult CheckGameEnds(IGameModeContext& ctx) override;
		std::uint32_t GetStateScore(const MpPlayerState& state) const override;
		void OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view) override;
		bool HasDamageAmplifierInOvertime() const override {
			return true;
		}

	protected:
		static constexpr float CampingLimitSecs = 120.0f;

		/** @brief Number of players that started the round, a round of a single player never ends by elimination */
		std::int32_t _participantCount;

		OvertimeRule GetOvertimeRule() const override {
			return OvertimeRule::TiedLeaders;
		}
		std::uint32_t GetLeaderScore(IGameModeContext& ctx, Actors::Player* player) override {
			return (std::uint32_t)std::max<std::int32_t>(ctx.GetPlayerState(player).ModeLives, 0);
		}
		/** @brief Called when a player who joined too late is taken out */
		virtual void OnLateJoinerOut(IGameModeContext& ctx, Actors::Player* player) { }
		/** @brief Announces the position of a player who doesn't roast anybody */
		void AnnouncePosition(IGameModeContext& ctx, Actors::Player* player);

		/** @brief Takes the player out of the round */
		void Eliminate(IGameModeContext& ctx, Actors::Player* player);
		/** @brief Returns the only player still in the round, `nullptr` if there are more of them or none */
		Actors::Player* FindLastStanding(IGameModeContext& ctx, std::int32_t& standingCount);
	};

	/**
		@brief Extended Last Rabbit Standing game mode (free-for-all)

		@ref LastRabbitStandingMode where every roast also scores a point. When only one player is left, the points of the
		survivor are multiplied by 1.5, of the last player eliminated by 1.25 and of the one before by 1.125, and the most
		points win.
	*/
	class ExtendedLastRabbitStandingMode : public LastRabbitStandingMode
	{
	public:
		ExtendedLastRabbitStandingMode();

		MpGameMode GetMode() const override {
			return MpGameMode::ExtendedLastRabbitStanding;
		}

		void OnRoundStarted(IGameModeContext& ctx) override;
		void OnPlayerKilled(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* killer) override;
		GameEndResult CheckGameEnds(IGameModeContext& ctx) override;
		GameEndResult OnTimeOut(IGameModeContext& ctx) override;
		std::uint32_t GetStateScore(const MpPlayerState& state) const override;
		void OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view) override;
		StringView GetScoreboardLabel() const override;

	protected:
		void OnLateJoinerOut(IGameModeContext& ctx, Actors::Player* player) override;

	private:
		bool _bonusApplied;

		void ApplyBonus(IGameModeContext& ctx);
	};
}

#endif
