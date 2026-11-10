#include "JailbreakMode.h"

#if defined(WITH_MULTIPLAYER)

#include "../ServerInitialization.h"
#include "../../Actors/Player.h"
#include "../../UI/Font.h"
#include "../../../nCine/I18n.h"
#include "../../../nCine/Base/FrameTimer.h"

#include <cmath>
#include <cstring>

#include <Base/Format.h>

namespace Jazz2::Multiplayer::GameModes
{
	JailbreakMode::JailbreakMode()
		: _jails{}, _jailsResolved(false), _teamPoints{}, _releaseTimeLeft(0.0f), _lastCountdown(0), _openTimeLeft(0.0f), _lastFreeTime{}
	{
	}

	void JailbreakMode::OnRoundStarted(IGameModeContext& ctx)
	{
		std::memset(_teamPoints, 0, sizeof(_teamPoints));
		std::memset(_lastFreeTime, 0, sizeof(_lastFreeTime));
		_releaseTimeLeft = 0.0f;
		_lastCountdown = 0;
		ResolveJails(ctx);

		ctx.GetGameModeObjects().clear();
		std::uint8_t teamCount = ctx.GetTeamCount();
		for (std::uint8_t team = 0; team < teamCount; team++) {
			// The level draws its own jail
			if (!_jails[team].HasArea) {
				AddObject(ctx, GameModeObjectType::Jail, team, _jails[team].Area.Center());
			}
		}
		ctx.SyncGameModeObjects();

		// Everybody starts in the jail (the start positions are there), the round start opens it
		for (auto* player : ctx.GetPlayers()) {
			if (!ctx.IsSpectating(player)) {
				auto& state = ctx.GetPlayerState(player);
				if (state.Team < MaxTeamCount && IsInJail(state.Team, player->GetPos())) {
					SetFlag(ctx, player, PlayerModeFlags::Jailed, true);
					ctx.SyncPlayerModeState(player);
				}
			}
		}
		ReleaseAll(ctx);
		ctx.SyncTeamScores();
	}

	void JailbreakMode::OnUpdate(IGameModeContext& ctx, float timeMult)
	{
		std::uint8_t teamCount = ctx.GetTeamCount();

		if (_releaseTimeLeft > 0.0f) {
			_releaseTimeLeft -= timeMult;
			// The release is counted down in the last 5 seconds
			std::int32_t secsLeft = (std::int32_t)std::ceil(_releaseTimeLeft / FrameTimer::FramesPerSecond);
			if (secsLeft != _lastCountdown && secsLeft > 0 && secsLeft <= (std::int32_t)ReleaseCountdownSecs) {
				_lastCountdown = secsLeft;
				ctx.ShowCountdownToAllPlayers(secsLeft);
			}
			if (_releaseTimeLeft <= 0.0f) {
				_releaseTimeLeft = 0.0f;
				_lastCountdown = 0;
				ctx.ShowCountdownToAllPlayers(0);
				ReleaseAll(ctx);
			}
		}

		// While a team has no free player (except at the end of a round) the jails stay open, then for 10 more seconds
		if (_releaseTimeLeft <= 0.0f && !AllTeamsHavePlayers(ctx, true)) {
			if (_openTimeLeft <= 0.0f) {
				ctx.SetTrigger(ReleaseTrigger, true);
			}
			_openTimeLeft = OpenSecs * FrameTimer::FramesPerSecond;
		} else if (_openTimeLeft > 0.0f) {
			_openTimeLeft -= timeMult;
			if (_openTimeLeft <= 0.0f) {
				_openTimeLeft = 0.0f;
				ctx.SetTrigger(ReleaseTrigger, false);
			}
		}

		for (auto* player : ctx.GetPlayers()) {
			if (ctx.IsSpectating(player) || player->GetHealth() <= 0) {
				continue;
			}
			auto& state = ctx.GetPlayerState(player);
			if (state.Team >= teamCount || !HasFlag(ctx, player, PlayerModeFlags::Jailed)) {
				continue;
			}

			const auto& jail = _jails[state.Team];
			bool inJail = IsInJail(state.Team, player->GetPos());
			if (!inJail) {
				if (jail.HasArea || _openTimeLeft > 0.0f) {
					// Out of the jail, the prisoner is free again
					SetFlag(ctx, player, PlayerModeFlags::Jailed, false);
					ctx.SyncPlayerModeState(player);
				} else {
					// Without walls in the level, the jail holds its prisoners by itself
					ctx.WarpPlayer(player, jail.Area.Center());
				}
			}
		}

		// The last free player of a team has 90 seconds to free the others, then dies
		if (_releaseTimeLeft <= 0.0f) {
			for (std::uint8_t team = 0; team < teamCount; team++) {
				std::int32_t members = ctx.CountPlayersWithRole(PlayerModeFlags::None, team);
				std::int32_t jailed = ctx.CountPlayersWithRole(PlayerModeFlags::Jailed, team);
				if (members >= 2 && members - jailed == 1) {
					_lastFreeTime[team] += timeMult;
					if (_lastFreeTime[team] >= LastFreePlayerSecs * FrameTimer::FramesPerSecond) {
						_lastFreeTime[team] = 0.0f;
						for (auto* player : ctx.GetPlayers()) {
							if (!ctx.IsSpectating(player) && ctx.GetPlayerState(player).Team == team &&
								!HasFlag(ctx, player, PlayerModeFlags::Jailed) && player->GetHealth() > 0) {
								ctx.KillPlayer(player);
								break;
							}
						}
					}
				} else {
					_lastFreeTime[team] = 0.0f;
				}
			}
		}
	}

	void JailbreakMode::OnPlayerKilled(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* killer)
	{
		// Whoever dies goes to jail
		JailPlayer(ctx, victim);
		if (_releaseTimeLeft <= 0.0f) {
			CheckTeamsJailed(ctx);
		}
	}

	void JailbreakMode::OnPlayerTeamChanged(IGameModeContext& ctx, Actors::Player* player)
	{
		// Measured in JJ2+: a team change sends the player to the new team's jail, but nobody scores
		JailPlayer(ctx, player);
	}

	RespawnDecision JailbreakMode::DecideRespawn(IGameModeContext& ctx, Actors::Player* player)
	{
		RespawnDecision decision = CustomGameModeBase::DecideRespawn(ctx, player);
		std::uint8_t team = ctx.GetPlayerState(player).Team;
		if (HasFlag(ctx, player, PlayerModeFlags::Jailed) && team < MaxTeamCount) {
			// The start positions of the team are in its jail, a level without them uses the middle of the jail
			if (!IsInJail(team, decision.SpawnPos)) {
				decision.SpawnPos = _jails[team].Area.Center();
			}
		}
		return decision;
	}

	bool JailbreakMode::CanDamagePlayer(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* attacker)
	{
		// Jailed players are invincible
		return !HasFlag(ctx, victim, PlayerModeFlags::Jailed);
	}

	bool JailbreakMode::CanPlayersPush(IGameModeContext& ctx, Actors::Player* a, Actors::Player* b)
	{
		return (!HasFlag(ctx, a, PlayerModeFlags::Jailed) && !HasFlag(ctx, b, PlayerModeFlags::Jailed));
	}

	bool JailbreakMode::CanFreezeActor(IGameModeContext& ctx, Actors::ActorBase* actor, Actors::Player* shooter)
	{
		ResolveJails(ctx);
		std::uint8_t teamCount = ctx.GetTeamCount();
		for (std::uint8_t team = 0; team < teamCount; team++) {
			if (!_jails[team].HasArea || !IsInJail(team, actor->GetPos())) {
				continue;
			}
			// Only teammates of the prisoners can freeze anything in a jail, and nobody in overtime
			if (ctx.IsOvertime() || shooter == nullptr) {
				return false;
			}
			return (ctx.GetPlayerState(shooter).Team == team);
		}
		return true;
	}

	bool JailbreakMode::TryGetTeamScore(IGameModeContext& ctx, std::uint8_t team, std::uint32_t& score)
	{
		score = (team < MaxTeamCount ? _teamPoints[team] : 0);
		return true;
	}

	std::uint32_t JailbreakMode::GetStateScore(const MpPlayerState& state) const
	{
		return state.Kills;
	}

	void JailbreakMode::OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view)
	{
		// How many players of each team are in jail, under its score
		std::uint8_t teamCount = ctx.GetTeamCount();
		for (std::uint8_t team = 0; team < teamCount; team++) {
			// TRANSLATORS: Shown in the HUD in Jailbreak under the score of a team, first {} is the number of its players in jail, second {} is the number of all its players
			DrawUnderTeamScore(ctx, hud, view, team, _f("jailed {}/{}", ctx.CountPlayersWithRole(PlayerModeFlags::Jailed, team),
				ctx.CountPlayersWithRole(PlayerModeFlags::None, team)));
		}

		if (HasFlag(ctx, player, PlayerModeFlags::Jailed)) {
			// TRANSLATORS: Status shown in the HUD in Jailbreak to a jailed player
			DrawStatusLine(hud, view, _("You are in jail!"), Colorf(0.68f, 0.36f, 0.36f));
		}
	}

	StringView JailbreakMode::GetRoleLabel(std::uint8_t modeFlags) const
	{
		if (modeFlags & (std::uint8_t)PlayerModeFlags::Jailed) {
			// TRANSLATORS: Label shown above a jailed player in Jailbreak, keep it short
			return _("Jailed");
		}
		return {};
	}

	bool JailbreakMode::ProcessCommand(IGameModeContext& ctx, StringView command)
	{
		if (command == "release"_s) {
			ReleaseAll(ctx);
			return true;
		}
		return false;
	}

	void JailbreakMode::OnOvertimeStarted(IGameModeContext& ctx)
	{
		CustomGameModeBase::OnOvertimeStarted(ctx);
		ctx.SetTrigger(OvertimeTrigger, true);
	}

	void JailbreakMode::ResolveJails(IGameModeContext& ctx)
	{
		if (_jailsResolved) {
			return;
		}
		_jailsResolved = true;

		// Two Area ID events at opposite corners of each jail, the first parameter bit is the team
		SmallVector<Pair<Vector2i, std::uint32_t>, 0> corners;
		ctx.FindOriginalEvents(AreaIdEvent, corners);

		for (std::uint8_t team = 0; team < MaxTeamCount; team++) {
			auto& jail = _jails[team];
			jail.HasArea = false;

			// Only the first two teams can have a jail in the level
			std::int32_t found = 0;
			Vector2i min, max;
			for (const auto& corner : corners) {
				if (team >= 2 || (corner.second() & 0x01) != (std::uint32_t)team) {
					continue;
				}
				Vector2i tile = corner.first();
				if (found == 0) {
					min = max = tile;
				} else {
					min = Vector2i(std::min(min.X, tile.X), std::min(min.Y, tile.Y));
					max = Vector2i(std::max(max.X, tile.X), std::max(max.Y, tile.Y));
				}
				found++;
			}

			if (found >= 2) {
				jail.HasArea = true;
				jail.Area = Rectf((float)min.X * 32.0f, (float)min.Y * 32.0f, (float)(max.X - min.X + 1) * 32.0f, (float)(max.Y - min.Y + 1) * 32.0f);
			} else if (team < ctx.GetTeamCount()) {
				Vector2f pos = ctx.GetTeamBasePosition(team);
				jail.Area = Rectf(pos.X - FallbackJailRadius, pos.Y - FallbackJailRadius, FallbackJailRadius * 2.0f, FallbackJailRadius * 2.0f);
			}
		}
	}

	bool JailbreakMode::IsInJail(std::uint8_t team, Vector2f pos) const
	{
		if (team >= MaxTeamCount) {
			return false;
		}
		const auto& area = _jails[team].Area;
		return (pos.X >= area.X && pos.X < area.X + area.W && pos.Y >= area.Y && pos.Y < area.Y + area.H);
	}

	void JailbreakMode::JailPlayer(IGameModeContext& ctx, Actors::Player* player)
	{
		if (SetFlag(ctx, player, PlayerModeFlags::Jailed, true)) {
			ctx.SyncPlayerModeState(player);
		}
	}

	void JailbreakMode::ReleaseAll(IGameModeContext& ctx)
	{
		// Trigger 30 lets the prisoners out (the level warps them), a prisoner stays jailed until out of the jail
		ctx.SetTrigger(ReleaseTrigger, true);
		_openTimeLeft = OpenSecs * FrameTimer::FramesPerSecond;
		std::memset(_lastFreeTime, 0, sizeof(_lastFreeTime));

		for (auto* player : ctx.GetPlayers()) {
			auto& state = ctx.GetPlayerState(player);
			if (state.Team < MaxTeamCount && !_jails[state.Team].HasArea && SetFlag(ctx, player, PlayerModeFlags::Jailed, false)) {
				ctx.SyncPlayerModeState(player);
			}
		}

		// TRANSLATORS: Message in the console of all players in Jailbreak when the jails open
		ctx.SendMessageToAllPlayers(_("*** All prisoners have been released"));
	}

	void JailbreakMode::CheckTeamsJailed(IGameModeContext& ctx)
	{
		if (!AllTeamsHavePlayers(ctx)) {
			return;
		}

		std::uint8_t teamCount = ctx.GetTeamCount();
		std::int32_t freeTeams = 0, jailedTeams = 0;
		std::uint8_t lastFreeTeam = 0, jailedTeam = 0;
		for (std::uint8_t team = 0; team < teamCount; team++) {
			std::int32_t members = ctx.CountPlayersWithRole(PlayerModeFlags::None, team);
			if (members == 0) {
				continue;
			}
			if (ctx.CountPlayersWithRole(PlayerModeFlags::Jailed, team) < members) {
				freeTeams++;
				lastFreeTeam = team;
			} else {
				jailedTeams++;
				jailedTeam = team;
			}
		}

		if (jailedTeams == 0) {
			return;
		}

		const auto& config = ctx.GetServerConfiguration();
		if (freeTeams == 1) {
			_teamPoints[lastFreeTeam]++;
			// TRANSLATORS: Message in the console of all players in Jailbreak when all enemies are jailed, {} is the team color name in English (Blue, Red, Green or Yellow)
			ctx.SendMessageToAllPlayers(_f("*** {} Team scored a point", GetTeamName(lastFreeTeam)));
		}

		// The round is over, everybody dies (only the losing team without ResetAllTeams) and the jails open later
		_releaseTimeLeft = ReleaseDelaySecs * FrameTimer::FramesPerSecond;
		_lastCountdown = 0;
		for (auto* player : ctx.GetPlayers()) {
			if (ctx.IsSpectating(player) || player->GetHealth() <= 0) {
				continue;
			}
			if (config.ResetAllTeams || freeTeams != 1 || ctx.GetPlayerState(player).Team != lastFreeTeam) {
				ctx.KillPlayer(player);
			}
		}
		ctx.SyncTeamScores();
	}

	bool JailbreakMode::AllTeamsHavePlayers(IGameModeContext& ctx, bool freeOnly)
	{
		std::uint8_t teamCount = ctx.GetTeamCount();
		for (std::uint8_t team = 0; team < teamCount; team++) {
			std::int32_t count = ctx.CountPlayersWithRole(PlayerModeFlags::None, team);
			if (freeOnly) {
				count -= ctx.CountPlayersWithRole(PlayerModeFlags::Jailed, team);
			}
			if (count <= 0) {
				return false;
			}
		}
		return true;
	}
}

#endif
