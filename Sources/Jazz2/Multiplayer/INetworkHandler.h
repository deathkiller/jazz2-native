#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "ConnectionResult.h"
#include "Peer.h"
#include "Reason.h"
#include "../../Main.h"

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::Multiplayer
{
	/**
		@brief Interface to handle network requests on a server
	
		@experimental
	*/
	class INetworkHandler
	{
	public:
		/** @brief Returns the server name */
		virtual StringView GetServerName() const = 0;
		/** @brief Sets the server name */
		virtual void SetServerName(StringView value) = 0;

		/** @brief Called when a peer connects to the server */
		virtual ConnectionResult OnPeerConnected(const Peer& peer, std::uint32_t clientData) = 0;
		/** @brief Called when a peer disconnects from the server */
		virtual void OnPeerDisconnected(const Peer& peer, Reason reason) = 0;
		/** @brief Called when a packet is received */
		virtual void OnPacketReceived(const Peer& peer, std::uint8_t channelId, std::uint8_t* data, std::size_t dataLength) = 0;
	};
}

#endif