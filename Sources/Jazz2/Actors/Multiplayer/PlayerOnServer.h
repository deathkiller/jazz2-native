#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Player.h"
#include "../../Multiplayer/PeerDescriptor.h"

using namespace Jazz2::Multiplayer;

namespace Jazz2::Multiplayer
{
	class MpLevelHandler;
}

namespace Jazz2::Actors::Multiplayer
{
	/** @brief Player in online session */
	class MpPlayer : public Player
	{
		DEATH_RUNTIME_OBJECT(Player);

		friend class MpLevelHandler;

	public:
		/** @brief Returns session peer descriptor */
		std::shared_ptr<PeerDescriptor> GetPeerDescriptor();

	protected:
		std::shared_ptr<PeerDescriptor> _peerDesc;
	};

	/** @brief Player on the server in online session */
	class PlayerOnServer : public MpPlayer
	{
		DEATH_RUNTIME_OBJECT(MpPlayer);

		friend class MpLevelHandler;

	public:
		PlayerOnServer();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;
		bool CanCauseDamage(ActorBase* collider) override;

	private:
		std::shared_ptr<ActorBase> _lastAttacker;

		bool IsAttacking() const;
	};
}

#endif