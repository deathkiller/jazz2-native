#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Main.h"

struct _ENetPeer;

namespace Jazz2::Multiplayer
{
	/** @brief Remote peer */
	struct Peer
	{
		Peer(std::nullptr_t = nullptr) : _enet(nullptr) {}
		Peer(_ENetPeer* peer) : _enet(peer) {}

		inline bool operator==(const Peer& dt) const {
			return (_enet == dt._enet);
		}
		inline bool operator!=(const Peer& dt) const {
			return (_enet != dt._enet);
		}

		explicit operator bool() const {
			return IsValid();
		}

		/** @brief Returns `true` if the peer is valid */
		bool IsValid() const {
			return (_enet != nullptr);
		}

#ifndef DOXYGEN_GENERATING_OUTPUT
		_ENetPeer* _enet;
#endif
	};
}

#endif