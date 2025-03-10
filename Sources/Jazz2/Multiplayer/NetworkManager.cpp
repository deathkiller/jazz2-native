#include "NetworkManager.h"

#if defined(WITH_MULTIPLAYER)

#include "ServerDiscovery.h"
#include "../PreferencesCache.h"
#include "../UI/DiscordRpcClient.h"

#include "../../simdjson/simdjson.h"

#include <Containers/StringUtils.h>
#include <Containers/StringStlView.h>
#include <IO/FileSystem.h>

using namespace Death;
using namespace Death::Containers::Literals;

using namespace std::string_view_literals;
using namespace simdjson;

namespace Jazz2::Multiplayer
{
	NetworkManager::NetworkManager()
	{
	}

	NetworkManager::~NetworkManager()
	{
	}

	void NetworkManager::CreateClient(INetworkHandler* handler, StringView address, std::uint16_t port, std::uint32_t clientData)
	{
		_clientConfig = std::make_unique<ClientConfiguration>();
		NetworkManagerBase::CreateClient(handler, address, port, clientData);
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
		ServerConfiguration serverConfig{};

		// Try to load configuration from "Jazz2.Server.config" file
		auto configPath = fs::CombinePath(PreferencesCache::GetDirectory(), "Jazz2.Server.config"_s);
		auto s = fs::Open(configPath, FileAccess::Read);
		auto fileSize = s->GetSize();
		if (fileSize >= 4 && fileSize < 64 * 1024 * 1024) {
			auto buffer = std::make_unique<char[]>(fileSize + simdjson::SIMDJSON_PADDING);
			s->Read(buffer.get(), fileSize);
			buffer[fileSize] = '\0';

			ondemand::parser parser;
			ondemand::document doc;
			if (parser.iterate(buffer.get(), fileSize, fileSize + simdjson::SIMDJSON_PADDING).get(doc) == SUCCESS) {
				LOGI("Loaded server configuration template from \"%s\"", configPath.data());

				std::string_view serverName;
				if (doc["ServerName"].get(serverName) == SUCCESS) {
					serverConfig.ServerName = serverName;
				}

				std::string_view serverPassword;
				if (doc["ServerPassword"].get(serverPassword) == SUCCESS) {
					serverConfig.ServerPassword = serverPassword;
				}

				std::string_view welcomeMessage;
				if (doc["WelcomeMessage"].get(welcomeMessage) == SUCCESS) {
					serverConfig.WelcomeMessage = welcomeMessage;
				}

				std::int64_t maxPlayerCount;
				if (doc["MaxPlayerCount"].get(maxPlayerCount) == SUCCESS && maxPlayerCount > 0 && maxPlayerCount <= UINT32_MAX) {
					serverConfig.MaxPlayerCount = (std::uint32_t)maxPlayerCount;
				}

				std::string_view gameMode;
				if (doc["GameMode"].get(gameMode) == SUCCESS) {
					auto gameModeString = StringUtils::lowercase(StringView(gameMode));
					if (gameModeString == "battle"sv || gameModeString == "b"sv) {
						serverConfig.GameMode = MpGameMode::Battle;
					} else if (gameModeString == "teambattle"_s || gameModeString == "tb"_s) {
						serverConfig.GameMode = MpGameMode::TeamBattle;
					} else if (gameModeString == "capturetheflag"_s || gameModeString == "ctf"_s) {
						serverConfig.GameMode = MpGameMode::CaptureTheFlag;
					} else if (gameModeString == "race"_s || gameModeString == "r"_s) {
						serverConfig.GameMode = MpGameMode::Race;
					} else if (gameModeString == "teamrace"_s || gameModeString == "tr"_s) {
						serverConfig.GameMode = MpGameMode::TeamRace;
					} else if (gameModeString == "treasurehunt"_s || gameModeString == "th"_s) {
						serverConfig.GameMode = MpGameMode::TreasureHunt;
					} else if (gameModeString == "cooperation"_s || gameModeString == "coop"_s || gameModeString == "c"_s) {
						serverConfig.GameMode = MpGameMode::Cooperation;
					}
				}

				std::int64_t serverPort;
				if (doc["ServerPort"].get(serverPort) == SUCCESS && serverPort > 0 && serverPort <= UINT16_MAX) {
					serverConfig.ServerPort = (std::uint16_t)serverPort;
				}

				bool isPrivate;
				if (doc["IsPrivate"].get(isPrivate) == SUCCESS) {
					serverConfig.IsPrivate = isPrivate;
				}

				std::int64_t allowedPlayerTypes;
				if (doc["AllowedPlayerTypes"].get(allowedPlayerTypes) == SUCCESS && allowedPlayerTypes > 0 && allowedPlayerTypes <= UINT8_MAX) {
					serverConfig.AllowedPlayerTypes = (std::uint8_t)allowedPlayerTypes;
				}

				std::int64_t idleKickTimeSecs;
				if (doc["IdleKickTimeSecs"].get(idleKickTimeSecs) == SUCCESS && idleKickTimeSecs >= INT32_MIN && idleKickTimeSecs <= INT32_MAX) {
					serverConfig.IdleKickTimeSecs = (std::int16_t)idleKickTimeSecs;
				}

				ondemand::object whitelistedUniquePlayerIDs;
				if (doc["WhitelistedUniquePlayerIDs"].get(whitelistedUniquePlayerIDs) == SUCCESS) {
					for (auto item : whitelistedUniquePlayerIDs) {
						std::string_view key;
						if (item.unescaped_key().get(key) == SUCCESS && !key.empty()) {
							std::string_view value;
							item.value().get(value);
							serverConfig.WhitelistedUniquePlayerIDs.emplace(key, value);
						}
					}
				}

				ondemand::object bannedUniquePlayerIDs;
				if (doc["BannedUniquePlayerIDs"].get(bannedUniquePlayerIDs) == SUCCESS) {
					for (auto item : bannedUniquePlayerIDs) {
						std::string_view key;
						if (item.unescaped_key().get(key) == SUCCESS && !key.empty()) {
							std::string_view value;
							item.value().get(value);
							serverConfig.BannedUniquePlayerIDs.emplace(key, value);
						}
					}
				}

				ondemand::object bannedIPAddresses;
				if (doc["BannedIPAddresses"].get(bannedIPAddresses) == SUCCESS) {
					for (auto item : bannedIPAddresses) {
						std::string_view key;
						if (item.unescaped_key().get(key) == SUCCESS && !key.empty()) {
							std::string_view value;
							item.value().get(value);
							serverConfig.BannedIPAddresses.emplace(key, value);
						}
					}
				}

				bool requiresDiscordAuth;
				if (doc["RequiresDiscordAuth"].get(requiresDiscordAuth) == SUCCESS) {
					serverConfig.RequiresDiscordAuth = requiresDiscordAuth;
				}
			} else {
				LOGE("Server configuration template from \"%s\" cannot be parsed", configPath.data());
			}
		}
		
		// Set default values
		if (serverConfig.ServerName.empty()) {
			serverConfig.ServerName = "{PlayerName}'s Server"_s;
		}
		if (serverConfig.WelcomeMessage.empty()) {
			serverConfig.WelcomeMessage = "Welcome to the {ServerName}!"_s;
		}
		if (serverConfig.ServerPort == 0) {
			serverConfig.ServerPort = 7438;
		}
		if (serverConfig.MaxPlayerCount == 0) {
			serverConfig.MaxPlayerCount = MaxPeerCount;
		}

		// Replace variables in parameters
		String playerName;
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
		if (PreferencesCache::EnableDiscordIntegration && UI::DiscordRpcClient::Get().IsSupported()) {
			playerName = UI::DiscordRpcClient::Get().GetUserDisplayName();
		}
#endif
		if (playerName.empty()) {
			playerName = PreferencesCache::PlayerName;
		}

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
		: IsAuthenticated(false), PreferredPlayerType(PlayerType::None)
	{
	}
}

#endif