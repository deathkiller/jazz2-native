#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "IGameMode.h"

namespace Jazz2::Multiplayer::GameModes
{
	/**
		@brief Base of all game modes

		Implements the optional parts of @ref IGameMode with neutral defaults: the mode owns its round logic, nothing
		restricts damage, pushing or freezing, a kill counts as usual, the score is the kills, and there are no
		overtime, commands, team numbers, scoreboard column or roles of its own. A mode overrides only what differs.
	*/
	class GameModeBase : public IGameMode
	{
	public:
		bool OwnsRoundLogic() const override {
			return true;
		}
		void OnPlayerTeamChanged(IGameModeContext& ctx, Actors::Player* player) override {
		}
		bool CanDamagePlayer(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* attacker) override {
			return true;
		}
		bool CanPlayersPush(IGameModeContext& ctx, Actors::Player* a, Actors::Player* b) override {
			return true;
		}
		bool PreventsHazardDeath(IGameModeContext& ctx, Actors::Player* player) override {
			return false;
		}
		bool CanFreezeActor(IGameModeContext& ctx, Actors::ActorBase* actor, Actors::Player* shooter) override {
			return true;
		}
		bool HasDamageAmplifierInOvertime() const override {
			return false;
		}
		bool ProcessCommand(IGameModeContext& ctx, StringView command) override {
			return false;
		}
		void OnOvertimeStarted(IGameModeContext& ctx) override {
		}
		std::int32_t GetDamage(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* attacker, std::int32_t amount) override {
			return amount;
		}
		void OnPlayerHit(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* attacker) override {
		}
		bool CountsAsKill(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* attacker) override {
			return true;
		}
		bool IsEliminated(IGameModeContext& ctx, Actors::Player* player) override {
			return (ctx.GetPlayerState(player).ModeFlags & (std::uint8_t)PlayerModeFlags::Eliminated) != 0;
		}
		bool TryGetTeamScore(IGameModeContext& ctx, std::uint8_t team, std::uint32_t& score) override {
			return false;
		}
		bool TryGetTeamExtra(IGameModeContext& ctx, std::uint8_t team, std::uint32_t& value) override {
			return false;
		}
		GameEndResult OnTimeOut(IGameModeContext& ctx) override {
			return GameEndResult::None();
		}
		std::uint32_t GetStateScore(const MpPlayerState& state) const override {
			return state.Kills;
		}
		StringView GetScoreboardLabel() const override {
			return {};
		}
		std::uint32_t GetForcedFurColor(std::uint8_t modeFlags) const override {
			return 0;
		}
		StringView GetRoleLabel(std::uint8_t modeFlags) const override {
			return {};
		}
	};
}

#endif
