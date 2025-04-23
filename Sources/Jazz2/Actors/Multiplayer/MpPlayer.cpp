#include "MpPlayer.h"

#if defined(WITH_MULTIPLAYER)

namespace Jazz2::Actors::Multiplayer
{
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