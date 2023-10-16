#pragma once

#if defined(WITH_MULTIPLAYER)

#include "Peer.h"
#include "../../Common.h"

namespace Jazz2::Multiplayer
{
	class INetworkHandler
	{
	public:
		virtual bool OnPeerConnected(const Peer& peer, std::uint32_t clientData) = 0;
		virtual void OnPeerDisconnected(const Peer& peer, std::uint32_t reason) = 0;
		virtual void OnPacketReceived(const Peer& peer, std::uint8_t channelId, std::uint8_t* data, std::size_t dataLength) = 0;
	};
}

#endif