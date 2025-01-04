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
		LocalPlayerOnServer();

	protected:
		void EmitWeaponFlare() override;
	};
}

#endif