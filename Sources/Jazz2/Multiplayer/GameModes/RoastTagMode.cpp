#include "RoastTagMode.h"

#if defined(WITH_MULTIPLAYER)

#include "../ServerInitialization.h"
#include "../../Actors/Player.h"
#include "../../../nCine/I18n.h"
#include "../../../nCine/Base/FrameTimer.h"

namespace Jazz2::Multiplayer::GameModes
{
	RoastTagMode::RoastTagMode()
		: _holderIndex(UINT32_MAX), _bottomFeederIndex(UINT32_MAX), _holderStreak(0), _holderIdleFrames(0.0f)
	{
	}

	void RoastTagMode::OnRoundStarted(IGameModeContext& ctx)
	{
		_holderIndex = UINT32_MAX;
		_bottomFeederIndex = UINT32_MAX;
		_holderStreak = 0;
		_holderIdleFrames = 0.0f;
	}

	void RoastTagMode::OnUpdate(IGameModeContext& ctx, float timeMult)
	{
		if (_holderIndex == UINT32_MAX) {
			return;
		}

		auto* holder = ctx.FindPlayerByIndex(_holderIndex);
		if (holder == nullptr || ctx.IsSpectating(holder)) {
			// The holder left, the ring is up for grabs again and there's no bottom feeder without it
			DropRing(ctx);
			UpdateBottomFeeder(ctx);
			return;
		}
		if (_bottomFeederIndex != UINT32_MAX) {
			auto* bottomFeeder = ctx.FindPlayerByIndex(_bottomFeederIndex);
			if (bottomFeeder == nullptr || ctx.IsSpectating(bottomFeeder)) {
				UpdateBottomFeeder(ctx);
			}
		}

		_holderIdleFrames += timeMult;
		if (_holderIdleFrames >= HolderIdleLimitSecs * FrameTimer::FramesPerSecond) {
			// The holder loses a point and dies
			AddPoints(ctx, holder, -1);
			ctx.ShowAlertToAllPlayers(ServerMessage(ServerMessageType::RoastTagLostRingIdle, ctx.GetPlayerName(holder)));
			DropRing(ctx);
			UpdateBottomFeeder(ctx);
			ctx.SyncPlayerModeState(holder);
			ctx.KillPlayer(holder);
		}
	}

	void RoastTagMode::OnPlayerKilled(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* killer)
	{
		std::uint32_t victimIndex = (std::uint32_t)victim->GetPlayerIndex();
		if (killer == nullptr || killer == victim) {
			if (victimIndex == _holderIndex) {
				DropRing(ctx);
			}
			UpdateBottomFeeder(ctx);
			return;
		}

		std::uint32_t killerIndex = (std::uint32_t)killer->GetPlayerIndex();
		if (_holderIndex == UINT32_MAX || victimIndex == _holderIndex) {
			// The first roast, or a roast of the holder, takes the ring
			AddPoints(ctx, killer, 3);
			GiveRing(ctx, killer);
		} else if (killerIndex == _holderIndex) {
			if (victimIndex == _bottomFeederIndex) {
				AddPoints(ctx, killer, 3);
			} else {
				_holderStreak++;
				AddPoints(ctx, killer, (std::int32_t)std::min<std::uint32_t>(_holderStreak, 5));
			}
			_holderIdleFrames = 0.0f;
		} else if (killerIndex == _bottomFeederIndex) {
			AddPoints(ctx, killer, 1);
		}

		UpdateBottomFeeder(ctx);
		ctx.SyncPlayerModeState(killer);
	}

	bool RoastTagMode::CanDamagePlayer(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* attacker)
	{
		if (_holderIndex == UINT32_MAX) {
			// Free-for-all until somebody takes the ring
			return true;
		}

		auto isSpecial = [this](Actors::Player* player) {
			std::uint32_t index = (std::uint32_t)player->GetPlayerIndex();
			return (index == _holderIndex || index == _bottomFeederIndex);
		};
		return (isSpecial(victim) || isSpecial(attacker));
	}

	void RoastTagMode::OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view)
	{
		CustomGameModeBase::OnDrawHUD(ctx, hud, player, view);

		Vector2f holderPos;
		if (!HasFlag(ctx, player, PlayerModeFlags::It) && ctx.FindPlayerWithRole(PlayerModeFlags::It, holderPos)) {
			hud.DrawTargetArrow(view, player, holderPos, Colorf(0.42f, 0.8f, 0.38f));
		}

		if (HasFlag(ctx, player, PlayerModeFlags::It)) {
			// TRANSLATORS: Status shown in the HUD in Roast Tag to the player holding the ring
			DrawStatusLine(hud, view, _("You have the ring!"), Colorf(0.42f, 0.62f, 0.38f));
		} else if (HasFlag(ctx, player, PlayerModeFlags::BottomFeeder)) {
			// TRANSLATORS: Status shown in the HUD in Roast Tag to the player with the fewest points, anyone can roast them
			DrawStatusLine(hud, view, _("You are the bottom feeder"), Colorf(0.68f, 0.36f, 0.36f));
		}
	}

	StringView RoastTagMode::GetScoreboardLabel() const
	{
		// TRANSLATORS: Column header in the multiplayer scoreboard (points in modes that score other than by kills), keep it short
		return _("Points");
	}

	StringView RoastTagMode::GetRoleLabel(std::uint8_t modeFlags) const
	{
		if (modeFlags & (std::uint8_t)PlayerModeFlags::It) {
			// TRANSLATORS: Label shown above the player holding the ring in Roast Tag, keep it short
			return _("Ring");
		}
		if (modeFlags & (std::uint8_t)PlayerModeFlags::BottomFeeder) {
			// TRANSLATORS: Label shown above the player with the fewest points in Roast Tag, keep it short
			return _("Bottom feeder");
		}
		return {};
	}

	void RoastTagMode::GiveRing(IGameModeContext& ctx, Actors::Player* player)
	{
		ClearFlag(ctx, _holderIndex, PlayerModeFlags::It);

		_holderIndex = (std::uint32_t)player->GetPlayerIndex();
		_holderStreak = 0;
		_holderIdleFrames = 0.0f;
		SetFlag(ctx, player, PlayerModeFlags::It, true);
		ctx.SyncPlayerModeState(player);

		ctx.ShowAlertToAllPlayers(ServerMessage(ServerMessageType::RoastTagHasRing, ctx.GetPlayerName(player)));
	}

	void RoastTagMode::DropRing(IGameModeContext& ctx)
	{
		ClearFlag(ctx, _holderIndex, PlayerModeFlags::It);
		_holderIndex = UINT32_MAX;
		_holderStreak = 0;
		_holderIdleFrames = 0.0f;
	}

	void RoastTagMode::UpdateBottomFeeder(IGameModeContext& ctx)
	{
		// The fewest points, then the most deaths, the lower player index on a tie
		Actors::Player* bottomFeeder = nullptr;
		for (auto* player : ctx.GetPlayers()) {
			if (!IsActive(ctx, player)) {
				continue;
			}
			if ((std::uint32_t)player->GetPlayerIndex() == _holderIndex) {
				continue;
			}
			if (bottomFeeder == nullptr) {
				bottomFeeder = player;
				continue;
			}
			auto& state = ctx.GetPlayerState(player);
			auto& bestState = ctx.GetPlayerState(bottomFeeder);
			if (state.ModePoints < bestState.ModePoints || (state.ModePoints == bestState.ModePoints && state.Deaths > bestState.Deaths)) {
				bottomFeeder = player;
			}
		}
		// Only while somebody holds the ring
		if (_holderIndex == UINT32_MAX) {
			bottomFeeder = nullptr;
		}

		std::uint32_t newIndex = (bottomFeeder != nullptr ? (std::uint32_t)bottomFeeder->GetPlayerIndex() : UINT32_MAX);
		if (newIndex == _bottomFeederIndex) {
			return;
		}

		ClearFlag(ctx, _bottomFeederIndex, PlayerModeFlags::BottomFeeder);
		_bottomFeederIndex = newIndex;
		if (bottomFeeder != nullptr) {
			SetFlag(ctx, bottomFeeder, PlayerModeFlags::BottomFeeder, true);
			ctx.SyncPlayerModeState(bottomFeeder);
		}
	}
}

#endif
