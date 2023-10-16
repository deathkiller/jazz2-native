#pragma once

#if defined(WITH_MULTIPLAYER)

#include "../../Common.h"

struct _ENetPeer;

namespace Jazz2::Multiplayer
{
	struct Peer
	{
		Peer(std::nullptr_t = nullptr) : _enet(nullptr) {}
		Peer(_ENetPeer* peer) : _enet(peer) {}

		inline bool operator==(const Peer& dt) const
		{
			return (_enet == dt._enet);
		}
		inline bool operator!=(const Peer& dt) const
		{
			return (_enet != dt._enet);
		}

		bool IsValid() const
		{
			return (_enet != nullptr);
		}

		_ENetPeer* _enet;
	};
}

#endif