#pragma once

#if defined(WITH_MULTIPLAYER)

#include "PlayerOnServer.h"

namespace Jazz2::Actors::Multiplayer
{
	class LocalPlayerOnServer : public PlayerOnServer
	{
		DEATH_RUNTIME_OBJECT(PlayerOnServer);

	public:
		LocalPlayerOnServer();

	protected:
		void EmitWeaponFlare() override;
	};
}

#endif