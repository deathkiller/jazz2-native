#include "TeamLastRabbitStandingMode.h"

#if defined(WITH_MULTIPLAYER)

#include "../ServerInitialization.h"
#include "../../Actors/Player.h"
#include "../../../nCine/I18n.h"
#include "../../../nCine/Base/Algorithms.h"
#include "../../../nCine/Base/Random.h"

#include <algorithm>

namespace Jazz2::Multiplayer::GameModes
{
	TeamLastRabbitStandingMode::TeamLastRabbitStandingMode()
		: _reserve{}, _teamPlaying{}, _outCounter(0)
	{
	}

	void TeamLastRabbitStandingMode::OnRoundStarted(IGameModeContext& ctx)
	{
		const auto& config = ctx.GetServerConfiguration();
		std::uint8_t teamCount = ctx.GetTeamCount();
		_participants.clear();
		_outCounter = 0;

		for (std::uint8_t team = 0; team < MaxTeamCount; team++) {
			_reserve[team] = 0;
			_teamPlaying[team] = false;
			if (team >= teamCount) {
				continue;
			}

			SmallVector<Actors::Player*, 0> members;
			for (auto* player : ctx.GetPlayers()) {
				if (!ctx.IsSpectating(player) && ctx.GetPlayerState(player).Team == team) {
					members.push_back(player);
				}
			}
			if (members.empty()) {
				continue;
			}
			_teamPlaying[team] = true;

			// Who gets the lives when a team has more players than it may is random
			Random().Shuffle(arrayView(members));
			std::int32_t holderCount = std::min((std::int32_t)members.size(), (std::int32_t)config.MaxTeamPlayersWithLives);
			nCine::sort(members.begin(), members.begin() + holderCount, [](Actors::Player* a, Actors::Player* b) {
				return (a->GetPlayerIndex() < b->GetPlayerIndex());
			});

			std::int32_t teamLives = (std::int32_t)config.TeamLives;
			std::int32_t given = 0;
			for (std::int32_t i = 0; i < (std::int32_t)members.size(); i++) {
				auto& state = ctx.GetPlayerState(members[i]);
				_participants.push_back((std::uint32_t)members[i]->GetPlayerIndex());
				if (i < holderCount) {
					state.ModeLives = std::min(GetShare(teamLives, i, holderCount), (std::int32_t)config.PlayerLives);
					given += state.ModeLives;
					ctx.SyncPlayerModeState(members[i]);
				} else {
					TakeOut(ctx, members[i]);
					ctx.SetPlayerOut(members[i], true);
				}
			}
			_reserve[team] = std::max(teamLives - given, 0);
		}

		ctx.SyncTeamScores();
	}

	void TeamLastRabbitStandingMode::OnUpdate(IGameModeContext& ctx, float timeMult)
	{
		// A player who joins after the round started is out, or waits for the reserve if late joiners are allowed
		for (auto* player : ctx.GetPlayers()) {
			if (ctx.IsSpectating(player)) {
				continue;
			}
			std::uint32_t index = (std::uint32_t)player->GetPlayerIndex();
			if (std::find(_participants.begin(), _participants.end(), index) != _participants.end()) {
				continue;
			}

			_participants.push_back(index);
			auto& state = ctx.GetPlayerState(player);
			state.ModeLives = 0;
			TakeOut(ctx, player);
			if (ctx.GetServerConfiguration().LateJoinersCanPlay && state.Team < MaxTeamCount && _teamPlaying[state.Team]) {
				DistributeReserve(ctx, state.Team);
			}
			if (IsEliminated(ctx, player)) {
				ctx.SetPlayerOut(player, true);
			}
		}
	}

	void TeamLastRabbitStandingMode::OnPlayerKilled(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* killer)
	{
		// Hurting a player who is out changes nothing
		if (IsEliminated(ctx, victim)) {
			return;
		}

		auto& state = ctx.GetPlayerState(victim);
		if (state.ModeLives > 0) {
			state.ModeLives--;
		}

		if (state.ModeLives <= 0) {
			TakeOut(ctx, victim);
			if (state.Team < MaxTeamCount) {
				DistributeReserve(ctx, state.Team);
			}
			if (IsEliminated(ctx, victim)) {
				ctx.ShowAlertToAllPlayers(_f("\n\n{} is out!", ctx.GetPlayerName(victim)));
			}
		}

		ctx.SyncPlayerModeState(victim);
		ctx.SyncTeamScores();
	}

	RespawnDecision TeamLastRabbitStandingMode::DecideRespawn(IGameModeContext& ctx, Actors::Player* player)
	{
		RespawnDecision decision = CustomGameModeBase::DecideRespawn(ctx, player);
		decision.CanRespawn = (ctx.GetPlayerState(player).ModeLives > 0);
		return decision;
	}

	GameEndResult TeamLastRabbitStandingMode::CheckGameEnds(IGameModeContext& ctx)
	{
		std::uint8_t teamCount = ctx.GetTeamCount();
		std::int32_t playingTeams = 0, aliveTeams = 0;
		std::uint8_t lastAliveTeam = 0;
		for (std::uint8_t team = 0; team < teamCount; team++) {
			if (!_teamPlaying[team]) {
				continue;
			}
			playingTeams++;
			if (ctx.GetTeamScore(team) > 0) {
				aliveTeams++;
				lastAliveTeam = team;
			}
		}

		if (playingTeams < 2 || aliveTeams > 1) {
			return GameEndResult::None();
		}
		return (aliveTeams == 1 ? GameEndResult::TeamWins(lastAliveTeam) : GameEndResult::Draw());
	}

	bool TeamLastRabbitStandingMode::TryGetTeamScore(IGameModeContext& ctx, std::uint8_t team, std::uint32_t& score)
	{
		// Lives of the players and the reserve of the team
		std::int32_t lives = (team < MaxTeamCount ? _reserve[team] : 0);
		for (auto* player : ctx.GetPlayers()) {
			auto& state = ctx.GetPlayerState(player);
			if (state.Team == team && !IsEliminated(ctx, player)) {
				lives += std::max(state.ModeLives, 0);
			}
		}
		score = (std::uint32_t)lives;
		return true;
	}

	std::uint32_t TeamLastRabbitStandingMode::GetStateScore(const MpPlayerState& state) const
	{
		return (std::uint32_t)std::max(state.ModeLives, 0);
	}

	void TeamLastRabbitStandingMode::OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view)
	{
		DrawScoreLine(ctx, hud, view, _("Lives"), (std::uint32_t)std::max(ctx.GetPlayerState(player).ModeLives, 0), false);
	}

	void TeamLastRabbitStandingMode::TakeOut(IGameModeContext& ctx, Actors::Player* player)
	{
		auto& state = ctx.GetPlayerState(player);
		state.ModeLives = 0;
		state.ModeCarried = ++_outCounter;
		if (state.DeathElapsedFrames == FLT_MAX) {
			state.DeathElapsedFrames = ctx.GetElapsedFrames();
		}
		SetFlag(ctx, player, PlayerModeFlags::Eliminated, true);
		ctx.SyncPlayerModeState(player);
	}

	void TeamLastRabbitStandingMode::DistributeReserve(IGameModeContext& ctx, std::uint8_t team)
	{
		if (_reserve[team] <= 0) {
			return;
		}

		const auto& config = ctx.GetServerConfiguration();
		SmallVector<Actors::Player*, 0> outPlayers;
		std::int32_t holders = 0;
		for (auto* player : ctx.GetPlayers()) {
			auto& state = ctx.GetPlayerState(player);
			if (state.Team != team) {
				continue;
			}
			if (IsEliminated(ctx, player)) {
				outPlayers.push_back(player);
			} else if (state.ModeLives > 0) {
				holders++;
			}
		}

		// Those out the longest first
		nCine::sort(outPlayers.begin(), outPlayers.end(), [&ctx](Actors::Player* a, Actors::Player* b) {
			return (ctx.GetPlayerState(a).ModeCarried < ctx.GetPlayerState(b).ModeCarried);
		});

		std::int32_t slots = std::max((std::int32_t)config.MaxTeamPlayersWithLives - holders, 0);
		std::int32_t count = std::min((std::int32_t)outPlayers.size(), slots);
		std::int32_t reserve = _reserve[team];
		for (std::int32_t i = 0; i < count; i++) {
			std::int32_t lives = std::min(GetShare(reserve, i, count), (std::int32_t)config.PlayerLives);
			if (lives <= 0) {
				continue;
			}

			auto* player = outPlayers[i];
			auto& state = ctx.GetPlayerState(player);
			_reserve[team] -= lives;
			state.ModeLives = lives;
			state.ModeCarried = 0;
			state.DeathElapsedFrames = FLT_MAX;
			SetFlag(ctx, player, PlayerModeFlags::Eliminated, false);
			ctx.SyncPlayerModeState(player);
			// Back in the game, if it was already watching
			ctx.SetPlayerOut(player, false);
		}
	}

	std::int32_t TeamLastRabbitStandingMode::GetShare(std::int32_t lives, std::int32_t index, std::int32_t count)
	{
		return (lives * (index + 1)) / count - (lives * index) / count;
	}
}

#endif
