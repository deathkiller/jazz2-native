#include "NetworkManager.h"

#if defined(WITH_MULTIPLAYER)

#include "ServerDiscovery.h"
#include "../ContentResolver.h"
#include "../PreferencesCache.h"
#include "../../nCine/I18n.h"

#include "../../simdjson/simdjson.h"

#include <Containers/DateTime.h>
#include <Containers/StringUtils.h>
#include <Containers/StringStlView.h>
#include <IO/FileSystem.h>

using namespace Death;
using namespace Death::Containers::Literals;

using namespace std::string_view_literals;
using namespace simdjson;

namespace Jazz2::Multiplayer
{
	PeerDescriptor::PeerDescriptor()
		: IsAuthenticated(false), IsAdmin(false), PreferredPlayerType(PlayerType::None), Points(0), PositionInRound(0),
			Player(nullptr), LevelState(PeerLevelState::Unknown), LastUpdated(0), EnableLedgeClimb(false), Team(0),
			Deaths(0), Kills(0), Laps(0), LapStarted{}, TreasureCollected(0), DeathElapsedFrames(FLT_MAX),
			LapsElapsedFrames(0.0f)
	{
	}

	NetworkManager::NetworkManager()
	{
	}

	NetworkManager::~NetworkManager()
	{
	}

	void NetworkManager::CreateClient(INetworkHandler* handler, StringView endpoint, std::uint16_t defaultPort, std::uint32_t clientData)
	{
		_peerDesc.emplace(Peer{}, std::make_shared<PeerDescriptor>());
		_serverConfig = std::make_unique<ServerConfiguration>();
		NetworkManagerBase::CreateClient(handler, endpoint, defaultPort, clientData);
	}

	bool NetworkManager::CreateServer(INetworkHandler* handler, ServerConfiguration&& serverConfig)
	{
		_peerDesc.emplace(Peer{}, std::make_shared<PeerDescriptor>());
		_serverConfig = std::make_unique<ServerConfiguration>(std::move(serverConfig));
		_serverConfig->StartUnixTimestamp = DateTime::Now().ToUnixMilliseconds() / 1000;
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

	ServerConfiguration& NetworkManager::GetServerConfiguration() const
	{
		return *_serverConfig;
	}

	std::uint32_t NetworkManager::GetPeerCount() const
	{
		return std::uint32_t(_peerDesc.size());
	}

	LockedPtr<const HashMap<Peer, std::shared_ptr<PeerDescriptor>>, Spinlock> NetworkManager::GetPeers()
	{
		return LockedPtr<const HashMap<Peer, std::shared_ptr<PeerDescriptor>>, Spinlock>(&_peerDesc, std::unique_lock<Spinlock>(_lock));
	}

	std::shared_ptr<PeerDescriptor> NetworkManager::GetPeerDescriptor(LocalPeerT)
	{
		std::unique_lock<Spinlock> l(_lock);
		auto it = _peerDesc.find(Peer{});
		return (it != _peerDesc.end() ? it->second : nullptr);
	}

	std::shared_ptr<PeerDescriptor> NetworkManager::GetPeerDescriptor(const Peer& peer)
	{
		if (!peer) {
			return nullptr;
		}

		std::unique_lock<Spinlock> l(_lock);
		auto it = _peerDesc.find(peer);
		return (it != _peerDesc.end() ? it->second : nullptr);
	}

	bool NetworkManager::HasInboundConnections() const
	{
		// Local peer is always present
		return (_peerDesc.size() > 1);
	}

	void NetworkManager::RefreshServerConfiguration()
	{
		if (_serverConfig->FilePath.empty()) {
			return;
		}

		HashMap<String, bool> includedFiles;

		FillServerConfigurationFromFile(_serverConfig->FilePath, *_serverConfig, includedFiles, 0);
		VerifyServerConfiguration(*_serverConfig);

		// Check if any newly banned player should be kicked
		std::unique_lock<Spinlock> l(_lock);
		for (auto& pair : _peerDesc) {
			if (pair.second->RemotePeer) {
				auto address = NetworkManagerBase::AddressToString(pair.second->RemotePeer);
				if (_serverConfig->BannedIPAddresses.contains(address)) {
					LOGI("[MP] Peer kicked \"%s\" (%s): Banned by IP address", pair.second->PlayerName.data(), address.data());
					Kick(pair.second->RemotePeer, Reason::Banned);
					continue;
				}

				auto uniquePlayerId = UuidToString(pair.second->Uuid);
				if (_serverConfig->BannedUniquePlayerIDs.contains(uniquePlayerId)) {
					LOGI("[MP] Peer kicked \"%s\" (%s): Banned by unique player ID", pair.second->PlayerName.data(), address.data());
					Kick(pair.second->RemotePeer, Reason::Banned);
					continue;
				}
			}
		}
	}

	ServerConfiguration NetworkManager::CreateDefaultServerConfiguration()
	{
		return LoadServerConfigurationFromFile("Jazz2.Server.config"_s);
	}

	ServerConfiguration NetworkManager::LoadServerConfigurationFromFile(StringView path)
	{
		HashMap<String, bool> includedFiles;

		ServerConfiguration serverConfig{};
		serverConfig.GameMode = MpGameMode::Cooperation;
		serverConfig.AllowedPlayerTypes = 0x01 | 0x02 | 0x04;
		serverConfig.MinPlayerCount = 1;
		serverConfig.PlaylistIndex = -1;
		serverConfig.PreGameSecs = 60;

		serverConfig.TotalPlayerPoints = 50;
		serverConfig.InitialPlayerHealth = 5;
		serverConfig.TotalKills = 10;
		serverConfig.TotalLaps = 3;
		serverConfig.TotalTreasureCollected = 100;

		FillServerConfigurationFromFile(path, serverConfig, includedFiles, 0);

		serverConfig.FilePath = path;

		VerifyServerConfiguration(serverConfig);

		return serverConfig;
	}

	void NetworkManager::FillServerConfigurationFromFile(StringView path, ServerConfiguration& serverConfig, HashMap<String, bool>& includedFiles, std::int32_t level)
	{
		auto configPath = fs::CombinePath(PreferencesCache::GetDirectory(), path);

		// Skip already included files to avoid infinite loops
		if (!includedFiles.emplace(configPath, true).second) {
			return;
		}

		auto s = fs::Open(configPath, FileAccess::Read);
		auto fileSize = s->GetSize();
		if (fileSize >= 4 && fileSize < 64 * 1024 * 1024) {
			auto buffer = std::make_unique<char[]>(fileSize + SIMDJSON_PADDING);
			s->Read(buffer.get(), fileSize);
			buffer[fileSize] = '\0';

			ContentResolver::StripCommentsFromJson(arrayView(buffer.get(), fileSize));

			ondemand::parser parser;
			ondemand::document doc;
			if (parser.iterate(buffer.get(), fileSize, fileSize + SIMDJSON_PADDING).get(doc) == SUCCESS) {
				if (level == 0) {
					LOGI("Loaded configuration from \"%s\"", configPath.data());
				} else {
					LOGI("Loaded configuration from \"%s\" because of $include directive", configPath.data());
				}

				std::string_view includeFile;
				if (doc["$include"].get(includeFile) == SUCCESS && !includeFile.empty()) {
					FillServerConfigurationFromFile(includeFile, serverConfig, includedFiles, level + 1);
				}

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
					serverConfig.MaxPlayerCount = std::uint32_t(maxPlayerCount);
				}

				std::int64_t minPlayerCount;
				if (doc["MinPlayerCount"].get(minPlayerCount) == SUCCESS && minPlayerCount >= 1 && minPlayerCount <= UINT32_MAX) {
					serverConfig.MinPlayerCount = std::uint32_t(minPlayerCount);
				}

				std::string_view gameMode;
				if (doc["GameMode"].get(gameMode) == SUCCESS) {
					serverConfig.GameMode = StringToGameMode(gameMode);
				}

				std::int64_t serverPort;
				if (doc["ServerPort"].get(serverPort) == SUCCESS && serverPort > 0 && serverPort <= UINT16_MAX) {
					serverConfig.ServerPort = std::uint16_t(serverPort);
				}

				bool isPrivate;
				if (doc["IsPrivate"].get(isPrivate) == SUCCESS) {
					serverConfig.IsPrivate = isPrivate;
				}

				bool requiresDiscordAuth;
				if (doc["RequiresDiscordAuth"].get(requiresDiscordAuth) == SUCCESS) {
					serverConfig.RequiresDiscordAuth = requiresDiscordAuth;
				}

				std::int64_t allowedPlayerTypes;
				if (doc["AllowedPlayerTypes"].get(allowedPlayerTypes) == SUCCESS && allowedPlayerTypes > 0 && allowedPlayerTypes <= UINT8_MAX) {
					serverConfig.AllowedPlayerTypes = std::uint8_t(allowedPlayerTypes);
				}

				std::int64_t idleKickTimeSecs;
				if (doc["IdleKickTimeSecs"].get(idleKickTimeSecs) == SUCCESS && idleKickTimeSecs >= INT32_MIN && idleKickTimeSecs <= INT32_MAX) {
					serverConfig.IdleKickTimeSecs = std::int16_t(idleKickTimeSecs);
				}

				ondemand::object adminUniquePlayerIDs;
				if (doc["AdminUniquePlayerIDs"].get(adminUniquePlayerIDs) == SUCCESS) {
					for (auto item : adminUniquePlayerIDs) {
						std::string_view key;
						if (item.unescaped_key().get(key) == SUCCESS && !key.empty()) {
							std::string_view value;
							item.value().get(value);
							serverConfig.AdminUniquePlayerIDs.emplace(key, value);
						}
					}
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

				// Game mode specific settings
				bool randomizePlaylist;
				if (doc["RandomizePlaylist"].get(randomizePlaylist) == SUCCESS) {
					serverConfig.RandomizePlaylist = randomizePlaylist;
				}

				bool isElimination;
				if (doc["IsElimination"].get(isElimination) == SUCCESS) {
					serverConfig.IsElimination = isElimination;
				}

				std::int64_t totalPlayerPoints;
				if (doc["TotalPlayerPoints"].get(totalPlayerPoints) == SUCCESS && totalPlayerPoints >= 0 && totalPlayerPoints <= INT32_MAX) {
					serverConfig.TotalPlayerPoints = std::uint32_t(totalPlayerPoints);
				}

				std::int64_t initialPlayerHealth;
				if (doc["InitialPlayerHealth"].get(initialPlayerHealth) == SUCCESS && initialPlayerHealth > 0 && initialPlayerHealth <= INT32_MAX) {
					serverConfig.InitialPlayerHealth = std::uint32_t(initialPlayerHealth);
				}

				std::int64_t maxGameTimeSecs;
				if (doc["MaxGameTimeSecs"].get(maxGameTimeSecs) == SUCCESS && maxGameTimeSecs >= 0 && maxGameTimeSecs <= INT32_MAX) {
					serverConfig.MaxGameTimeSecs = std::uint32_t(maxGameTimeSecs);
				}

				std::int64_t preGameSecs;
				if (doc["PreGameSecs"].get(preGameSecs) == SUCCESS && preGameSecs >= 0 && preGameSecs <= INT32_MAX) {
					serverConfig.PreGameSecs = std::uint32_t(preGameSecs);
				}

				std::int64_t totalKills;
				if (doc["TotalKills"].get(totalKills) == SUCCESS && totalKills >= 0 && totalKills <= INT32_MAX) {
					serverConfig.TotalKills = std::uint32_t(totalKills);
				}

				std::int64_t totalLaps;
				if (doc["TotalLaps"].get(totalLaps) == SUCCESS && totalLaps >= 0 && totalLaps <= INT32_MAX) {
					serverConfig.TotalLaps = std::uint32_t(totalLaps);
				}

				std::int64_t totalTreasureCollected;
				if (doc["TotalTreasureCollected"].get(totalTreasureCollected) == SUCCESS && totalTreasureCollected >= 0 && totalTreasureCollected <= INT32_MAX) {
					serverConfig.TotalTreasureCollected = std::uint32_t(totalTreasureCollected);
				}

				// Playlist
				ondemand::array playlist;
				if (doc["Playlist"].get(playlist) == SUCCESS) {
					serverConfig.Playlist.clear();

					for (auto entry : playlist) {
						// Playlist entry inherits all properties from the main server configuration
						PlaylistEntry playlistEntry {};
						playlistEntry.GameMode = serverConfig.GameMode;
						playlistEntry.IsElimination = serverConfig.IsElimination;
						playlistEntry.InitialPlayerHealth = serverConfig.InitialPlayerHealth;
						playlistEntry.MaxGameTimeSecs = serverConfig.MaxGameTimeSecs;
						playlistEntry.PreGameSecs = serverConfig.PreGameSecs;
						playlistEntry.TotalKills = serverConfig.TotalKills;
						playlistEntry.TotalLaps = serverConfig.TotalLaps;
						playlistEntry.TotalTreasureCollected = serverConfig.TotalTreasureCollected;

						std::string_view levelName;
						if (entry["LevelName"].get(levelName) == SUCCESS) {
							playlistEntry.LevelName = levelName;
						}

						std::string_view gameMode;
						if (entry["GameMode"].get(gameMode) == SUCCESS) {
							playlistEntry.GameMode = StringToGameMode(gameMode);
						}

						bool isElimination;
						if (entry["IsElimination"].get(isElimination) == SUCCESS) {
							playlistEntry.IsElimination = isElimination;
						}

						std::int64_t initialPlayerHealth;
						if (entry["InitialPlayerHealth"].get(initialPlayerHealth) == SUCCESS && initialPlayerHealth > 0 && initialPlayerHealth <= INT32_MAX) {
							playlistEntry.InitialPlayerHealth = std::uint32_t(initialPlayerHealth);
						}

						std::int64_t maxGameTimeSecs;
						if (entry["MaxGameTimeSecs"].get(maxGameTimeSecs) == SUCCESS && maxGameTimeSecs >= 0 && maxGameTimeSecs <= INT32_MAX) {
							playlistEntry.MaxGameTimeSecs = std::uint32_t(maxGameTimeSecs);
						}

						std::int64_t preGameSecs;
						if (entry["PreGameSecs"].get(preGameSecs) == SUCCESS && preGameSecs >= 0 && preGameSecs <= INT32_MAX) {
							playlistEntry.PreGameSecs = std::uint32_t(preGameSecs);
						}

						std::int64_t totalKills;
						if (entry["TotalKills"].get(totalKills) == SUCCESS && totalKills >= 0 && totalKills <= INT32_MAX) {
							playlistEntry.TotalKills = std::uint32_t(totalKills);
						}

						std::int64_t totalLaps;
						if (entry["TotalLaps"].get(totalLaps) == SUCCESS && totalLaps >= 0 && totalLaps <= INT32_MAX) {
							playlistEntry.TotalLaps = std::uint32_t(totalLaps);
						}

						std::int64_t totalTreasureCollected;
						if (entry["TotalTreasureCollected"].get(totalTreasureCollected) == SUCCESS && totalTreasureCollected >= 0 && totalTreasureCollected <= INT32_MAX) {
							playlistEntry.TotalTreasureCollected = std::uint32_t(totalTreasureCollected);
						}

						if (!playlistEntry.LevelName.empty() && serverConfig.GameMode != MpGameMode::Unknown) {
							serverConfig.Playlist.push_back(std::move(playlistEntry));
						}
					}
				}

				std::int64_t playlistIndex;
				if (doc["PlaylistIndex"].get(playlistIndex) == SUCCESS && playlistIndex >= -1 && playlistIndex < serverConfig.Playlist.size()) {
					serverConfig.PlaylistIndex = std::uint32_t(playlistIndex);
				}
				} else {
					LOGE("Configuration from \"%s\" cannot be parsed", configPath.data());
			}
		}
	}

	void NetworkManager::VerifyServerConfiguration(ServerConfiguration& serverConfig)
	{
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
		auto playerName = PreferencesCache::GetEffectivePlayerName();
		if (playerName.empty()) {
			playerName = "Unknown"_s;
		}

		serverConfig.ServerName = StringUtils::replaceAll(serverConfig.ServerName, "{PlayerName}"_s, playerName);

		serverConfig.WelcomeMessage = StringUtils::replaceAll(serverConfig.WelcomeMessage, "{PlayerName}"_s, playerName);
		serverConfig.WelcomeMessage = StringUtils::replaceAll(serverConfig.WelcomeMessage, "{ServerName}"_s, serverConfig.ServerName);
	}

	StringView NetworkManager::GameModeToLocalizedString(MpGameMode mode)
	{
		switch (mode) {
			case MpGameMode::Battle: return _("Battle");
			case MpGameMode::TeamBattle: return _("Team Battle");
			case MpGameMode::Race: return _("Race");
			case MpGameMode::TeamRace: return _("Team Race");
			case MpGameMode::TreasureHunt: return _("Treasure Hunt");
			case MpGameMode::TeamTreasureHunt: return _("Team Treasure Hunt");
			case MpGameMode::CaptureTheFlag: return _("Capture The Flag");
			case MpGameMode::Cooperation: return _("Cooperation");
			default: return _("Unknown");
		}
	}

	MpGameMode NetworkManager::StringToGameMode(StringView value)
	{
		auto gameModeString = StringUtils::lowercase(value);
		if (gameModeString == "battle"sv || gameModeString == "b"sv) {
			return MpGameMode::Battle;
		} else if (gameModeString == "teambattle"_s || gameModeString == "tb"_s) {
			return MpGameMode::TeamBattle;
		} else if (gameModeString == "race"_s || gameModeString == "r"_s) {
			return MpGameMode::Race;
		} else if (gameModeString == "teamrace"_s || gameModeString == "tr"_s) {
			return MpGameMode::TeamRace;
		} else if (gameModeString == "treasurehunt"_s || gameModeString == "th"_s) {
			return MpGameMode::TreasureHunt;
		} else if (gameModeString == "teamtreasurehunt"_s || gameModeString == "tth"_s) {
			return MpGameMode::TeamTreasureHunt;
		} else if (gameModeString == "capturetheflag"_s || gameModeString == "ctf"_s) {
			return MpGameMode::CaptureTheFlag;
		} else if (gameModeString == "cooperation"_s || gameModeString == "coop"_s || gameModeString == "c"_s) {
			return MpGameMode::Cooperation;
		} else {
			return MpGameMode::Unknown;
		}
	}

	String NetworkManager::UuidToString(StaticArrayView<16, std::uint8_t> uuid)
	{
		String uuidStr{NoInit, 39};
		std::int32_t uuidStrLength = formatString(uuidStr.data(), uuidStr.size() + 1, "%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X",
			uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7], uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
		DEATH_DEBUG_ASSERT(uuidStr.size() == uuidStrLength);
		return uuidStr;
	}

	ConnectionResult NetworkManager::OnPeerConnected(const Peer& peer, std::uint32_t clientData)
	{
		bool isListening = (GetState() == NetworkState::Listening);

		if (isListening) {
			auto address = NetworkManagerBase::AddressToString(peer);
			if (_serverConfig->BannedIPAddresses.contains(address)) {
				LOGI("[MP] Peer kicked \"<unknown>\" (%s): Banned by IP address", address.data());
				return Reason::Banned;
			}
		}

		ConnectionResult result = NetworkManagerBase::OnPeerConnected(peer, clientData);

		if (result && isListening) {
			std::unique_lock<Spinlock> l(_lock);
			auto [peerDesc, inserted] = _peerDesc.emplace(peer, std::make_shared<PeerDescriptor>());
			peerDesc->second->RemotePeer = peer;
		}

		return result;
	}

	void NetworkManager::OnPeerDisconnected(const Peer& peer, Reason reason)
	{
		NetworkManagerBase::OnPeerDisconnected(peer, reason);

		if (GetState() == NetworkState::Listening) {
			std::unique_lock<Spinlock> l(_lock);
			auto it = _peerDesc.find(peer);
			if (it != _peerDesc.end()) {
				it->second->RemotePeer = {};
				_peerDesc.erase(it);
			}
		}
	}
}

#endif