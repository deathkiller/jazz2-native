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

	void MpPlayer::UpdatePlayerStacking(float timeMult, bool snap)
	{
		auto* mpHandler = static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler);
		Actors::ActorBase* ground = mpHandler->FindPlayerToStandOn(this, timeMult);
		if (ground != nullptr) {
			// Carrying makes CanJump() return true (so we can jump off) and Player::OnUpdate zeroes our vertical
			// speed so we rest instead of falling through.
			if (snap) {
				// Reposition so our feet rest on the other player's head. The head is derived from the other
				// player's position plus OUR collision head-offset (players are the same size) rather than its
				// AABB top - a RemoteActor's AABB is the sprite box, which is taller and would leave us floating.
				// Re-snapping each frame also lets us ride the other player's vertical movement.
				float groundHead = ground->GetPos().Y + (AABBInner.T - _pos.Y);
				float delta = groundHead - AABBInner.B;
				if (delta != 0.0f) {
					MoveInstantly(Vector2f(0.0f, delta), MoveType::Relative);
				}
			}
			UpdateCarryingObject(ground);
			_stackCarrying = true;
		} else if (_stackCarrying) {
			// We were standing on a player and aren't anymore (walked off / jumped) - stop carrying so we fall
			// again. Guarded by _stackCarrying so we never cancel a real solid object we might be standing on.
			CancelCarryingObject();
			_stackCarrying = false;
		}
	}

	std::shared_ptr<PeerDescriptor> MpPlayer::GetPeerDescriptor()
	{
		return _peerDesc;
	}

	std::shared_ptr<const PeerDescriptor> MpPlayer::GetPeerDescriptor() const
	{
		return _peerDesc;
	}
}

#endif