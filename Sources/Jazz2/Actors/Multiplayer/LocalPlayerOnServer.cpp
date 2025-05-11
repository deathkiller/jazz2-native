#include "LocalPlayerOnServer.h"

#if defined(WITH_MULTIPLAYER)

#include "../../PreferencesCache.h"

namespace Jazz2::Actors::Multiplayer
{
	LocalPlayerOnServer::LocalPlayerOnServer(std::shared_ptr<PeerDescriptor> peerDesc)
	{
		_peerDesc = std::move(peerDesc);
		_peerDesc->Player = this;
	}

	void LocalPlayerOnServer::SetCurrentWeapon(WeaponType weaponType, SetCurrentWeaponReason reason)
	{
		if (reason == SetCurrentWeaponReason::AddAmmo && !PreferencesCache::SwitchToNewWeapon) {
			return;
		}

		PlayerOnServer::SetCurrentWeapon(weaponType, reason);
	}

	void LocalPlayerOnServer::EmitWeaponFlare()
	{
		PlayerOnServer::EmitWeaponFlare();

		// TODO: Broadcast flare of local players too
	}
}

#endif