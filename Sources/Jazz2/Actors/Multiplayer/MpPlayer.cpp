#include "MpPlayer.h"

#if defined(WITH_MULTIPLAYER)

#include "../../Multiplayer/MpLevelHandler.h"

namespace Jazz2::Actors::Multiplayer
{
	Task<bool> MpPlayer::OnActivatedAsync(const Actors::ActorActivationDetails& details)
	{
		bool success = async_await Player::OnActivatedAsync(details);

		// Apply carried-over progression (online co-op level change / reconnect) AFTER the base activation has
		// finished, so the weapons/lives/score/gems it just reset aren't wiped. When coroutines are enabled the
		// base OnActivatedAsync suspends on resource loading and only completes the reset here, so applying the
		// carryover at the spawn call site (right after OnActivated() returns) would be overwritten - it has to
		// happen here. The descriptor is populated by the server (level-change/disconnect capture) and on the
		// client from the CreateControllablePlayer packet. The flag is cleared by the owner (server: the spawn
		// packet site after it has been serialized; client: RemotablePlayer) so it isn't re-applied on respawn.
		if (_peerDesc != nullptr && _peerDesc->HasCarryOver) {
			ReceiveLevelCarryOver(ExitType::Normal, _peerDesc->CarryOver);
		}

		async_return success;
	}

	void MpPlayer::OnHandleSpectate(float timeMult)
	{
		auto* mpLevelHandler = static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler);

		// Only the spectator sitting in front of this screen drives its own camera; the server-side shadow of a
		// remote spectator gets its position from the owning client like any other remote player
		if (!mpLevelHandler->IsLocalPlayer(this)) {
			Player::OnHandleSpectate(timeMult);
			return;
		}

		Vector2f followedPos;
		if (_levelHandler->PlayerActionHit(this, PlayerAction::Fire)) {
			mpLevelHandler->CycleSpectateFollow(1);
		} else if (mpLevelHandler->GetSpectateFollowPlayerPos(followedPos)) {
			// While locked onto a player the movement keys aren't flying the camera, so they step between players
			if (_levelHandler->PlayerActionHit(this, PlayerAction::Right)) {
				mpLevelHandler->CycleSpectateFollow(1);
			} else if (_levelHandler->PlayerActionHit(this, PlayerAction::Left)) {
				mpLevelHandler->CycleSpectateFollow(-1);
			} else if (_levelHandler->PlayerActionHit(this, PlayerAction::Run)) {
				mpLevelHandler->StopSpectateFollow();
			}
		}

		if (mpLevelHandler->GetSpectateFollowPlayerPos(followedPos)) {
			// Snap the (invisible, collision-less) spectator onto the followed player - the camera targets this
			// actor, so it cuts over to the new player the same way a broadcast camera would
			_pos = followedPos;
			ResetPathTracking();
			return;
		}

		Player::OnHandleSpectate(timeMult);
	}

	std::shared_ptr<PeerDescriptor> MpPlayer::GetPeerDescriptor()
	{
		return _peerDesc;
	}

	std::shared_ptr<const PeerDescriptor> MpPlayer::GetPeerDescriptor() const
	{
		return _peerDesc;
	}

	bool MpPlayer::IsLedgeClimbAllowed() const
	{
		// The server decides whether ledge climbing can be used at all, the local player can only turn it off
		auto* mpLevelHandler = static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler);
		return (mpLevelHandler->IsLedgeClimbAllowed() && Player::IsLedgeClimbAllowed());
	}
}

#endif