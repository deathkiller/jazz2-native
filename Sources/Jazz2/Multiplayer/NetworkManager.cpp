#include "NetworkManager.h"

#if defined(WITH_MULTIPLAYER)

#include "ServerDiscovery.h"
#include "../PreferencesCache.h"
#include "../UI/DiscordRpcClient.h"

#include <Containers/StringUtils.h>

using namespace Death;
using namespace Death::Containers::Literals;

namespace Jazz2::Multiplayer
{
	NetworkManager::NetworkManager()
	{
	}

	NetworkManager::~NetworkManager()
	{
	}

	bool NetworkManager::CreateClient(INetworkHandler* handler, StringView address, std::uint16_t port, std::uint32_t clientData)
	{
		_clientConfig = std::make_unique<ClientConfiguration>();
		return NetworkManagerBase::CreateClient(handler, address, port, clientData);
	}

	bool NetworkManager::CreateServer(INetworkHandler* handler, ServerConfiguration&& serverConfig)
	{
		_serverConfig = std::make_unique<ServerConfiguration>(std::move(serverConfig));
		bool result = NetworkManagerBase::CreateServer(handler, _serverConfig->ServerPort);

		if (result && !_serverConfig->IsPrivate) {
			_discovery = std::make_unique<ServerDiscovery>(this);
		}

		return result;
	}

	void NetworkManager::Dispose()
	{
		NetworkManagerBase::Dispose();

		_discovery = nullptr;
	}

	ClientConfiguration& NetworkManager::GetClientConfiguration()
	{
		return *_clientConfig.get();
	}

	ServerConfiguration& NetworkManager::GetServerConfiguration()
	{
		return *_serverConfig.get();
	}

	std::uint32_t NetworkManager::GetPeerCount() const
	{
		// TODO: Include server itself
		return (std::uint32_t)_peerDesc.size() + 1;
	}

	NetworkManager::PeerDesc* NetworkManager::GetPeerDescriptor(const Peer& peer)
	{
		auto it = _peerDesc.find(peer);
		return (it != _peerDesc.end() ? &it->second : nullptr);
	}

	ServerConfiguration NetworkManager::CreateDefaultServerConfiguration()
	{
		ServerConfiguration serverConfig;
		// TODO: Load this from template file
		// TODO: Hardcoded name and port
		serverConfig.ServerName = "{PlayerName}'s Server"_s;
		serverConfig.WelcomeMessage = "Welcome to the {ServerName}!"_s;
		serverConfig.ServerPort = 7438;
		serverConfig.MaxPlayerCount = MaxPeerCount;

		String playerName;
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
		if (PreferencesCache::EnableDiscordIntegration && UI::DiscordRpcClient::Get().IsSupported()) {
			playerName = UI::DiscordRpcClient::Get().GetUserDisplayName();
		}
#endif
		if (playerName.empty()) {
			playerName = PreferencesCache::PlayerName;
		}

		// Replace variables in parameters
		serverConfig.ServerName = StringUtils::replaceAll(serverConfig.ServerName, "{PlayerName}"_s, playerName);

		serverConfig.WelcomeMessage = StringUtils::replaceAll(serverConfig.WelcomeMessage, "{PlayerName}"_s, playerName);
		serverConfig.WelcomeMessage = StringUtils::replaceAll(serverConfig.WelcomeMessage, "{ServerName}"_s, serverConfig.ServerName);

		return serverConfig;
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
		: Uuid1(0), Uuid2(0), IsAuthenticated(false), PreferredPlayerType(PlayerType::None)
	{
	}
}

#endif