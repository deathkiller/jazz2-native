#include "NetworkManager.h"

#if defined(WITH_MULTIPLAYER)

using namespace Death;

namespace Jazz2::Multiplayer
{
	NetworkManager::NetworkManager()
	{
	}

	NetworkManager::~NetworkManager()
	{
	}

	NetworkManager::PeerDesc* NetworkManager::GetPeerDescriptor(const Peer& peer)
	{
		auto it = _peerDesc.find(peer);
		return (it != _peerDesc.end() ? &it->second : nullptr);
	}

	ConnectionResult NetworkManager::OnPeerConnected(const Peer& peer, std::uint32_t clientData)
	{
		ConnectionResult result = NetworkManagerBase::OnPeerConnected(peer, clientData);

		if (result && GetState() == NetworkState::Listening) {
			_peerDesc[peer] = {};
		}

		return result;
	}

	void NetworkManager::OnPeerDisconnected(const Peer& peer, Reason reason)
	{
		NetworkManagerBase::OnPeerDisconnected(peer, reason);

		if (GetState() == NetworkState::Listening) {
			_peerDesc.erase(peer);
		}
	}

	NetworkManager::PeerDesc::PeerDesc()
		: PreferredPlayerType(PlayerType::None)
	{
	}
}

#endif