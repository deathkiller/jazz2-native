#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "ConnectionResult.h"
#include "Peer.h"
#include "Reason.h"

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::Multiplayer
{
	/**
		@brief Interface to handle incomming network requests
	
		@experimental
	*/
	class INetworkHandler
	{
	public:
		/** @brief Called when a peer connects to the local server or the local client connects to a server */
		virtual ConnectionResult OnPeerConnected(const Peer& peer, std::uint32_t clientData) = 0;
		/** @brief Called when a peer disconnects from the local server or the local client disconnects from a server */
		virtual void OnPeerDisconnected(const Peer& peer, Reason reason) = 0;
		/** @brief Called when a packet is received */
		virtual void OnPacketReceived(const Peer& peer, std::uint8_t channelId, std::uint8_t packetType, ArrayView<const std::uint8_t> data) = 0;
	};
}

#endif