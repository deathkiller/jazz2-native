#include "LocalPlayerOnServer.h"

#if defined(WITH_MULTIPLAYER)

namespace Jazz2::Actors::Multiplayer
{
	LocalPlayerOnServer::LocalPlayerOnServer(std::shared_ptr<PeerDescriptor> peerDesc)
	{
		_peerDesc = std::move(peerDesc);
		_peerDesc->Player = this;
	}

	void LocalPlayerOnServer::EmitWeaponFlare()
	{
		PlayerOnServer::EmitWeaponFlare();

		// TODO: Broadcast flare of local players too
	}
}

#endif