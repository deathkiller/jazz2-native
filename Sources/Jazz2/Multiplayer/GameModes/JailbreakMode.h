#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "CustomGameModeBase.h"
#include "../Teams.h"

#include "../../../nCine/Primitives/Rect.h"

namespace Jazz2::Multiplayer::GameModes
{
	/**
		@brief Jailbreak game mode (team-based)

		Every roasted player goes to the jail of their team, an area between two Area ID events of the level (the first
		parameter bit is the team), where the start positions of the team are. A prisoner is free again once out of
		the area: through a spring of the jail frozen by a teammate (only teammates can freeze them, and nobody in
		overtime), or when the jails open. Trigger 30 is on for 10 seconds whenever the jails open, the level should
		warp the prisoners out with it. Jailed players can't be hurt or pushed. When all players of a team are in jail,
		the other team scores a point, everybody dies (or only the losing team without @ref ServerConfiguration::ResetAllTeams)
		and the jails open 10 seconds later. The last free player of a team has 90 seconds to free the others.
	*/
	class JailbreakMode : public CustomGameModeBase
	{
	public:
		JailbreakMode();

		MpGameMode GetMode() const override {
			return MpGameMode::Jailbreak;
		}
		bool IsTeamBased() const override {
			return true;
		}

		void OnRoundStarted(IGameModeContext& ctx) override;
		void OnUpdate(IGameModeContext& ctx, float timeMult) override;
		void OnPlayerKilled(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* killer) override;
		void OnPlayerTeamChanged(IGameModeContext& ctx, Actors::Player* player) override;
		RespawnDecision DecideRespawn(IGameModeContext& ctx, Actors::Player* player) override;
		bool CanDamagePlayer(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* attacker) override;
		bool CanPlayersPush(IGameModeContext& ctx, Actors::Player* a, Actors::Player* b) override;
		bool CanFreezeActor(IGameModeContext& ctx, Actors::ActorBase* actor, Actors::Player* shooter) override;
		bool TryGetTeamScore(IGameModeContext& ctx, std::uint8_t team, std::uint32_t& score) override;
		std::uint32_t GetStateScore(const MpPlayerState& state) const override;
		void OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view) override;
		StringView GetRoleLabel(std::uint8_t modeFlags) const override;
		bool ProcessCommand(IGameModeContext& ctx, StringView command) override;
		void OnOvertimeStarted(IGameModeContext& ctx) override;

	private:
		static constexpr std::uint8_t ReleaseTrigger = 30;
		static constexpr std::uint8_t OvertimeTrigger = 29;
		static constexpr std::uint8_t AreaIdEvent = 242;
		static constexpr float ReleaseDelaySecs = 10.0f;
		static constexpr float ReleaseCountdownSecs = 5.0f;
		static constexpr float OpenSecs = 10.0f;
		static constexpr float LastFreePlayerSecs = 90.0f;
		// Levels without jail areas get a jail of this size around the base of the team
		static constexpr float FallbackJailRadius = 48.0f;

		struct Jail {
			// The middle is where prisoners are put
			Rectf Area;
			bool HasArea;
		};

		Jail _jails[MaxTeamCount];
		bool _jailsResolved;
		std::uint32_t _teamPoints[MaxTeamCount];
		float _releaseTimeLeft;
		std::int32_t _lastCountdown;
		float _openTimeLeft;
		float _lastFreeTime[MaxTeamCount];

		void ResolveJails(IGameModeContext& ctx);
		bool IsInJail(std::uint8_t team, Vector2f pos) const;
		void JailPlayer(IGameModeContext& ctx, Actors::Player* player);
		void ReleaseAll(IGameModeContext& ctx);
		void CheckTeamsJailed(IGameModeContext& ctx);
		bool AllTeamsHavePlayers(IGameModeContext& ctx, bool freeOnly = false);
	};
}

#endif
