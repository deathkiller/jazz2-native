#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "PlayerOnServer.h"

namespace Jazz2::Actors::Multiplayer
{
	/** @brief Local player in online session */
	class LocalPlayerOnServer : public PlayerOnServer
	{
		DEATH_RUNTIME_OBJECT(PlayerOnServer);

	public:
		LocalPlayerOnServer(std::shared_ptr<PeerDescriptor> peerDesc);

	protected:
		void SetCurrentWeapon(WeaponType weaponType, SetCurrentWeaponReason reason) override;

		/** @brief Emits weapon flare */
		void EmitWeaponFlare() override;
	};
}

#endif