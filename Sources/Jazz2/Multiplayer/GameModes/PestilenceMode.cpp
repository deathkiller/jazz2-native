#include "PestilenceMode.h"

#if defined(WITH_MULTIPLAYER)

#include "../ServerInitialization.h"
#include "../../Actors/Player.h"
#include "../../../nCine/I18n.h"
#include "../../../nCine/Base/FrameTimer.h"
#include "../../../nCine/Base/Random.h"

#include <algorithm>

namespace Jazz2::Multiplayer::GameModes
{
	PestilenceMode::PestilenceMode()
		: _noInfectionFrames(0.0f), _lastZombieKillerIndex(UINT32_MAX), _allInfected(false)
	{
	}

	void PestilenceMode::OnRoundStarted(IGameModeContext& ctx)
	{
		_participants.clear();
		_noInfectionFrames = 0.0f;
		_lastZombieKillerIndex = UINT32_MAX;
		_allInfected = false;

		for (auto* player : ctx.GetPlayers()) {
			if (!ctx.IsSpectating(player)) {
				_participants.push_back((std::uint32_t)player->GetPlayerIndex());
			}
		}

		// The last survivor of the previous round starts this one as a zombie
		for (auto* player : ctx.GetPlayers()) {
			auto& state = ctx.GetPlayerState(player);
			if (state.ModeCarryOverFlags & FirstZombieFlag) {
				state.ModeCarryOverFlags &= ~FirstZombieFlag;
				if (!ctx.IsSpectating(player)) {
					Infect(ctx, player, nullptr);
				}
			}
		}
	}

	void PestilenceMode::OnUpdate(IGameModeContext& ctx, float timeMult)
	{
		if (_allInfected) {
			return;
		}

		std::int32_t humanCount;
		std::int32_t zombieCount = CountZombies(ctx, humanCount);

		// A late joiner becomes a zombie right away
		for (auto* player : ctx.GetPlayers()) {
			if (ctx.IsSpectating(player) || HasFlag(ctx, player, PlayerModeFlags::Zombie)) {
				continue;
			}
			std::uint32_t index = (std::uint32_t)player->GetPlayerIndex();
			if (std::find(_participants.begin(), _participants.end(), index) == _participants.end()) {
				_participants.push_back(index);
				if (!ctx.GetServerConfiguration().LateJoinersCanPlay) {
					Infect(ctx, player, nullptr);
				}
			}
		}

		if (zombieCount == 0) {
			// Free-for-all until the first death, or again when all zombies left
			_noInfectionFrames = 0.0f;
			return;
		}

		_noInfectionFrames += timeMult;
		if (_noInfectionFrames >= NoInfectionLimitSecs * FrameTimer::FramesPerSecond && humanCount > 0) {
			_noInfectionFrames = 0.0f;
			ctx.SendMessageToAllPlayers(ServerMessage(ServerMessageType::PestilenceRandomZombie));
			std::int32_t pick = Random().Next(0, humanCount);
			for (auto* player : ctx.GetPlayers()) {
				if (!ctx.IsSpectating(player) && !HasFlag(ctx, player, PlayerModeFlags::Zombie) && pick-- == 0) {
					Infect(ctx, player, nullptr);
					break;
				}
			}
		}
	}

	void PestilenceMode::OnPlayerKilled(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* killer)
	{
		if (_allInfected) {
			return;
		}

		bool killerIsOther = (killer != nullptr && killer != victim);
		if (HasFlag(ctx, victim, PlayerModeFlags::Zombie)) {
			// A zombie stays a zombie, the human that roasted it scores
			if (killerIsOther && !HasFlag(ctx, killer, PlayerModeFlags::Zombie)) {
				std::int32_t humanCount;
				std::int32_t zombieCount = CountZombies(ctx, humanCount);
				AddPoints(ctx, killer, std::min<std::int32_t>(zombieCount, 3));
				ctx.SyncPlayerModeState(killer);
			}
			return;
		}

		Actors::Player* infector = (killerIsOther && HasFlag(ctx, killer, PlayerModeFlags::Zombie) ? killer : nullptr);
		if (infector != nullptr) {
			AddPoints(ctx, infector, 1);
			_noInfectionFrames = 0.0f;
			_lastZombieKillerIndex = (std::uint32_t)infector->GetPlayerIndex();
		}
		Infect(ctx, victim, infector);
	}

	bool PestilenceMode::CanDamagePlayer(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* attacker)
	{
		std::int32_t humanCount;
		if (CountZombies(ctx, humanCount) == 0) {
			return true;
		}
		return (HasFlag(ctx, victim, PlayerModeFlags::Zombie) != HasFlag(ctx, attacker, PlayerModeFlags::Zombie));
	}

	std::int32_t PestilenceMode::GetDamage(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* attacker, std::int32_t amount)
	{
		// Zombies do extra damage, 1 heart as the Damage Amplifier
		return (HasFlag(ctx, attacker, PlayerModeFlags::Zombie) ? amount + 1 : amount);
	}

	GameEndResult PestilenceMode::CheckGameEnds(IGameModeContext& ctx)
	{
		if (_allInfected) {
			return GameEndResult::PlayerWins(FindBestPlayer(ctx));
		}
		return CustomGameModeBase::CheckGameEnds(ctx);
	}

	void PestilenceMode::OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view)
	{
		CustomGameModeBase::OnDrawHUD(ctx, hud, player, view);

		if (HasFlag(ctx, player, PlayerModeFlags::Zombie)) {
			// TRANSLATORS: Status shown in the HUD in Pestilence to an infected player
			DrawStatusLine(hud, view, _("You are a zombie!"), Colorf(0.42f, 0.62f, 0.38f));
		}
	}

	StringView PestilenceMode::GetScoreboardLabel() const
	{
		return _("Points");
	}

	std::uint32_t PestilenceMode::GetForcedFurColor(std::uint8_t modeFlags) const
	{
		// Green in every fur section, the hue-shifted variant of the gradient (see ApplyTeamFurColor())
		return ((modeFlags & (std::uint8_t)PlayerModeFlags::Zombie) != 0 ? 0xD8D8D8D8u : 0u);
	}

	StringView PestilenceMode::GetRoleLabel(std::uint8_t modeFlags) const
	{
		if (modeFlags & (std::uint8_t)PlayerModeFlags::Zombie) {
			// TRANSLATORS: Label shown above an infected player in Pestilence, keep it short
			return _("Zombie");
		}
		return {};
	}

	void PestilenceMode::Infect(IGameModeContext& ctx, Actors::Player* player, Actors::Player* infector)
	{
		if (!SetFlag(ctx, player, PlayerModeFlags::Zombie, true)) {
			return;
		}
		ctx.SyncPlayerModeState(player);
		if (infector != nullptr) {
			ctx.SyncPlayerModeState(infector);
		}

		std::int32_t humanCount;
		CountZombies(ctx, humanCount);
		if (humanCount == 0 && _participants.size() >= 2) {
			// The last human fell
			_allInfected = true;
			AddPoints(ctx, player, 5);
			ctx.GetPlayerState(player).ModeCarryOverFlags |= FirstZombieFlag;
			ctx.SyncPlayerModeState(player);
			// The zombie that got the last roast, also when the last human was picked at random
			Actors::Player* lastKiller = (infector != nullptr ? infector :
				_lastZombieKillerIndex != UINT32_MAX ? ctx.FindPlayerByIndex(_lastZombieKillerIndex) : nullptr);
			if (lastKiller != nullptr && lastKiller != player) {
				AddPoints(ctx, lastKiller, 2);
				ctx.SyncPlayerModeState(lastKiller);
			}
			ctx.ShowAlertToAllPlayers(ServerMessage(ServerMessageType::PestilenceLastSurvivor, ctx.GetPlayerName(player)));
			return;
		}

		ctx.ShowAlertToAllPlayers(ServerMessage(ServerMessageType::PestilenceBecameZombie, ctx.GetPlayerName(player)));
	}

	std::int32_t PestilenceMode::CountZombies(IGameModeContext& ctx, std::int32_t& humanCount)
	{
		std::int32_t zombieCount = 0;
		humanCount = 0;
		for (auto* player : ctx.GetPlayers()) {
			if (ctx.IsSpectating(player)) {
				continue;
			}
			if (HasFlag(ctx, player, PlayerModeFlags::Zombie)) {
				zombieCount++;
			} else {
				humanCount++;
			}
		}
		return zombieCount;
	}
}

#endif
