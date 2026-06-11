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

	void LocalPlayerOnServer::OnUpdate(float timeMult)
	{
		// Host is simulated on the server - resolve standing on another player locally before the base update,
		// so jump input sees CanJump() == true (same as the client does for its own player)
		UpdatePlayerStacking(timeMult, /*snap:*/ true);

		PlayerOnServer::OnUpdate(timeMult);
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