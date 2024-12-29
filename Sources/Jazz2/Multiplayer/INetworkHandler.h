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
	class INetworkHandler
	{
	public:
		virtual StringView GetServerName() const = 0;
		virtual void SetServerName(StringView value) = 0;

		virtual ConnectionResult OnPeerConnected(const Peer& peer, std::uint32_t clientData) = 0;
		virtual void OnPeerDisconnected(const Peer& peer, Reason reason) = 0;
		virtual void OnPacketReceived(const Peer& peer, std::uint8_t channelId, std::uint8_t* data, std::size_t dataLength) = 0;
	};
}

#endif