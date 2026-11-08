#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "CustomGameModeBase.h"

namespace Jazz2::Multiplayer::GameModes
{
	/**
		@brief Roast Tag game mode (free-for-all)

		The first roast of the round gives 3 points and the ring, its holder is "it". While the ring is held, only
		the holder and the bottom feeder (the player with the fewest points) can be roasted or roast others. The
		holder scores 1, 2, 3, ... points for consecutive roasts (at most 5 per roast, always 3 for the bottom
		feeder), the bottom feeder scores 1 point per roast and whoever roasts the holder takes the ring. A holder
		that doesn't roast anybody for 90 seconds loses a point and the ring. The first player to reach the score wins.
	*/
	class RoastTagMode : public CustomGameModeBase
	{
	public:
		RoastTagMode();

		MpGameMode GetMode() const override {
			return MpGameMode::RoastTag;
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
		bool HasDamageAmplifierInOvertime() const override {
			return true;
		}
		void OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view) override;
		StringView GetScoreboardLabel() const override;
		StringView GetRoleLabel(std::uint8_t modeFlags) const override;

	private:
		static constexpr float HolderIdleLimitSecs = 90.0f;

		std::uint32_t _holderIndex;
		std::uint32_t _bottomFeederIndex;
		std::uint32_t _holderStreak;
		float _holderIdleFrames;

		void GiveRing(IGameModeContext& ctx, Actors::Player* player);
		void DropRing(IGameModeContext& ctx);
		void UpdateBottomFeeder(IGameModeContext& ctx);
	};
}

#endif
