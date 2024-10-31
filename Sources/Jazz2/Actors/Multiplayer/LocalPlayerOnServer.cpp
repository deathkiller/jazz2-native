#include "LocalPlayerOnServer.h"

#if defined(WITH_MULTIPLAYER)

namespace Jazz2::Actors::Multiplayer
{
	LocalPlayerOnServer::LocalPlayerOnServer()
	{
	}

	void LocalPlayerOnServer::EmitWeaponFlare()
	{
		PlayerOnServer::EmitWeaponFlare();

		// TODO: Broadcast flare of local players too
	}
}

#endif