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