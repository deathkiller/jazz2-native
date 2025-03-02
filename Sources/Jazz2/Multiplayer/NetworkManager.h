#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "NetworkManagerBase.h"
#include "../PlayerType.h"

#include "../../nCine/Base/HashMap.h"

namespace Jazz2::Multiplayer
{
	/**
		@brief Manages game-specific network connections

		@experimental
	*/
	class NetworkManager : public NetworkManagerBase
	{
	public:
		/** @brief Peer descriptor */
		struct PeerDesc {
			/** @brief Preferred player type selected by the peer */
			PlayerType PreferredPlayerType;
			/** @brief Player display name */
			String PlayerName;
			/** @brief Whether the peer is already authenticated */
			bool Authenticated;

			PeerDesc();
		};

		NetworkManager();
		~NetworkManager();

		NetworkManager(const NetworkManager&) = delete;
		NetworkManager& operator=(const NetworkManager&) = delete;

		PeerDesc* GetPeerDescriptor(const Peer& peer);

	protected:
		ConnectionResult OnPeerConnected(const Peer& peer, std::uint32_t clientData) override;
		void OnPeerDisconnected(const Peer& peer, Reason reason) override;

	private:
		HashMap<Peer, PeerDesc> _peerDesc; // Server: Per peer description
	};
}

#endif