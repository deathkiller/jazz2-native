#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "CustomGameModeBase.h"

#include <Containers/SmallVector.h>

namespace Jazz2::Multiplayer::GameModes
{
	/**
		@brief Pestilence game mode (free-for-all)

		The first player to die becomes a zombie. Zombies can only roast humans and humans can only roast zombies, a
		human roasted by a zombie (or dying in any other way) turns into a zombie too. A human scores 1 point per
		zombie in the game (at most 3) for roasting a zombie, a zombie scores 1 point for roasting a human. When the
		last human falls, the zombie that roasted them gets 2 more points, the last human 5 more and the round ends,
		the most points win. If no zombie roasts anybody for 120 seconds, a random human turns. Anybody who joins
		after the round started is a zombie, unless late joiners are allowed.
	*/
	class PestilenceMode : public CustomGameModeBase
	{
	public:
		PestilenceMode();

		MpGameMode GetMode() const override {
			return MpGameMode::Pestilence;
		}
		bool IsTeamBased() const override {
			return false;
		}

		void OnRoundStarted(IGameModeContext& ctx) override;
		void OnUpdate(IGameModeContext& ctx, float timeMult) override;
		void OnPlayerKilled(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* killer) override;
		bool CanDamagePlayer(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* attacker) override;
		bool CanPlayersPush(IGameModeContext& ctx, Actors::Player* a, Actors::Player* b) override {
			return CanDamagePlayer(ctx, a, b);
		}
		std::int32_t GetDamage(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* attacker, std::int32_t amount) override;
		bool HasDamageAmplifierInOvertime() const override {
			return true;
		}
		GameEndResult CheckGameEnds(IGameModeContext& ctx) override;
		void OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view) override;
		StringView GetScoreboardLabel() const override;
		std::uint32_t GetForcedFurColor(std::uint8_t modeFlags) const override;
		StringView GetRoleLabel(std::uint8_t modeFlags) const override;

	private:
		static constexpr float NoInfectionLimitSecs = 120.0f;
		static constexpr std::uint8_t FirstZombieFlag = 0x01;

		SmallVector<std::uint32_t, 0> _participants;
		float _noInfectionFrames;
		std::uint32_t _lastZombieKillerIndex;
		bool _allInfected;

		void Infect(IGameModeContext& ctx, Actors::Player* player, Actors::Player* infector);
		std::int32_t CountZombies(IGameModeContext& ctx, std::int32_t& humanCount);
	};
}

#endif
