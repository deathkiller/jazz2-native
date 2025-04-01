#include "LocalPlayerOnServer.h"

#if defined(WITH_MULTIPLAYER)

namespace Jazz2::Actors::Multiplayer
{
	LocalPlayerOnServer::LocalPlayerOnServer()
	{
		_peerDesc = std::make_unique<PeerDescriptor>();
		_peerDesc->Player = this;
	}

	void LocalPlayerOnServer::EmitWeaponFlare()
	{
		PlayerOnServer::EmitWeaponFlare();

		// TODO: Broadcast flare of local players too
	}
}

#endif