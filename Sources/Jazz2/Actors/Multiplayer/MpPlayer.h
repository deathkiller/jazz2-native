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

		friend class Jazz2::Multiplayer::MpLevelHandler;

	public:
		/** @brief Returns session peer descriptor */
		std::shared_ptr<PeerDescriptor> GetPeerDescriptor();

		/** @brief Returns session peer descriptor */
		std::shared_ptr<const PeerDescriptor> GetPeerDescriptor() const;

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		std::shared_ptr<PeerDescriptor> _peerDesc;
#endif
	};
}

#endif