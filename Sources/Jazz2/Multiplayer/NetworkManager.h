#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "NetworkManagerBase.h"
#include "MpGameMode.h"
#include "ServerInitialization.h"
#include "../PlayerType.h"

namespace Jazz2::Multiplayer
{
	/**
		@brief Client configuration received from the server
	*/
	struct ClientConfiguration
	{
		/** @brief Multiplayer game mode */
		MpGameMode GameMode;
		/** @brief Welcome message in the lobby */
		String WelcomeMessage;

		/** @brief Earned points in the current session (championship) */
		std::uint32_t Points;
	};

	/**
		@brief Manages game-specific network connections

		@experimental
	*/
	class NetworkManager : public NetworkManagerBase
	{
	public:
		/** @brief Peer descriptor */
		struct PeerDesc {
			/** @brief Unique Player ID */
			StaticArray<16, std::uint8_t> Uuid;
			/** @brief Whether the peer is already successfully authenticated */
			bool IsAuthenticated;
			/** @brief Whether the peer has admin rights */
			bool IsAdmin;
			/** @brief Preferred player type selected by the peer */
			PlayerType PreferredPlayerType;
			/** @brief Player display name */
			String PlayerName;
			/** @brief Earned points in the current session (championship) */
			std::uint32_t Points;

			PeerDesc();
		};

		NetworkManager();
		~NetworkManager();

		NetworkManager(const NetworkManager&) = delete;
		NetworkManager& operator=(const NetworkManager&) = delete;

		void CreateClient(INetworkHandler* handler, StringView endpoint, std::uint16_t defaultPort, std::uint32_t clientData) override;

		/** @brief Creates a server that accepts incoming connections */
		virtual bool CreateServer(INetworkHandler* handler, ServerConfiguration&& serverConfig);

		void Dispose() override;

		/** @brief Returns client configuration */
		ClientConfiguration& GetClientConfiguration() const;

		/** @brief Returns server configuration */
		ServerConfiguration& GetServerConfiguration() const;

		/** @brief Returns connected peer count */
		std::uint32_t GetPeerCount() const;
		/** @brief Returns global (session) peer descriptor for the specified connected peer */
		PeerDesc* GetPeerDescriptor(const Peer& peer);

		/**
		 * @brief Creates a default server configuration from the default template file
		 *
		 * For a description of the file format, see @ref LoadServerConfigurationFromFile().
		 */
		static ServerConfiguration CreateDefaultServerConfiguration();
		
		/**
		 * @brief Loads a server configuration from the specified file
		 *
		 * This method reads a JSON configuration file. If the JSON contains a @cpp "$include" @ce directive,
		 * this method recursively loads the referenced file.
		 *
		 * The parsed JSON file may contain the following fields:
		 * - @cpp "ServerName" @ce : (string) The name of the server
		 * - @cpp "ServerPassword" @ce : (string) Password to join the server
		 * - @cpp "WelcomeMessage" @ce : (string) Message displayed to players upon joining
		 * - @cpp "MaxPlayerCount" @ce : (integer) Maximum number of players allowed
		 * - @cpp "MinPlayerCount" @ce : (integer) Minimum number of players required
		 * - @cpp "ServerPort" @ce : (integer) The port number on which the server runs
		 * - @cpp "IsPrivate" @ce : (bool) Whether the server is private
		 * - @cpp "RequiresDiscordAuth" @ce : (bool) If `true`, requires Discord authentication
		 * - @cpp "AllowedPlayerTypes" @ce : (integer) Bitmask for allowed player types (1 = Jazz, 2 = Spaz, 4 = Lori)
		 * - @cpp "IdleKickTimeSecs" @ce : (integer) Time in seconds after idle players are kicked
		 * - @cpp "AdminUniquePlayerIDs" @ce : (object) Map of admin player IDs
		 *   - Key specifies player ID, value specified, value contains privileges
		 * - @cpp "WhitelistedUniquePlayerIDs" @ce : (object) Map of whitelisted player IDs
		 *   - Key specifies player ID, value can contain a user-defined comment
		 * - @cpp "BannedUniquePlayerIDs" @ce : (object) Map of banned player IDs
		 *   - Key specifies player ID, value can contain a user-defined comment (e.g., reason)
		 * - @cpp "BannedIPAddresses" @ce : (object) Map of banned IP addresses
		 *   - Key specifies IP address, value can contain a user-defined comment (e.g., reason)
		 * - @cpp "RandomizePlaylist" @ce : (bool) Whether to play the playlist in random order
		 * - @cpp "TotalPlayerPoints" @ce : (integer) Total points to win the championship
		 * - @cpp "GameMode" @ce : (string) Game mode
		 *   - @cpp "b" @ce / @cpp "battle" @ce - Battle
		 *   - @cpp "tb" @ce / @cpp "teambattle" @ce - Team Battle
		 *   - @cpp "ctf" @ce / @cpp "capturetheflag" @ce - Capture the Flag
		 *   - @cpp "r" @ce / @cpp "race" @ce - Race
		 *   - @cpp "tr" @ce / @cpp "teamrace" @ce - Team Race
		 *   - @cpp "th" @ce / @cpp "treasurehunt" @ce - Treasure Hunt
		 *   - @cpp "c" @ce / @cpp "coop" @ce / @cpp "cooperation" @ce - Cooperation
		 * - @cpp "IsElimination" @ce : (bool) Whether elimination mode is enabled
		 *   - If enabled, a player has a limited number of lives given by @cpp "TotalKills" @ce
		 *   - Can be combined with any game mode
		 * - @cpp "InitialPlayerHealth" @ce : (integer) Initial health of players (default is 5)
		 * - @cpp "MaxGameTimeSecs" @ce : (integer) Maximum allowed game time in seconds per level
		 * - @cpp "PreGameSecs" @ce : (integer) Pre-game duration in seconds (default is 60)
		 * - @cpp "TotalKills" @ce : (integer) Number of kills required to win (Battle)
		 * - @cpp "TotalLaps" @ce : (integer) Number of laps required to win (Race)
		 * - @cpp "TotalTreasureCollected" @ce : (integer) Number of treasures required to win (Tresure Hunt)
		 * - @cpp "Playlist" @ce : (array) List of game configurations per round, each entry may contain:
		 *   - @cpp "LevelName" @ce : (string) Name of the level
		 *   - @cpp "GameMode" @ce : (string) Specific game mode for this round
		 *   - @cpp "IsElimination" @ce : (bool) Whether elimination mode is enabled for this round
		 *   - @cpp "InitialPlayerHealth" @ce : (integer) Initial health of players for this round
		 *   - @cpp "MaxGameTimeSecs" @ce : (integer) Maximum game duration for this round
		 *   - @cpp "PreGameSecs" @ce : (integer) Pre-game duration before this round starts
		 *   - @cpp "TotalKills" @ce : (integer) Number of kills required to win this round (Battle)
		 *   - @cpp "TotalLaps" @ce : (integer) Number of laps required to win this round (Race)
		 *   - @cpp "TotalTreasureCollected" @ce : (integer) Total treasure required to win this round (Tresure Hunt)
		 * - @cpp "PlaylistIndex" @ce : (integer) Index of the current playlist entry
		 *
		 * If a property is missing in a playlist entry, it will inherit the value from the root configuration.
		 * `{PlayerName}` and `{ServerName}` variables can be used in @cpp "ServerName" @ce and @cpp "WelcomeMessage" @ce properties.
		 *
		 * Example the server configuration:
		 *
		 * @include ServerConfiguration.json
		 */
		static ServerConfiguration LoadServerConfigurationFromFile(StringView path);
		/** @brief Converts @ref MpGameMode to the localized string representation */
		static StringView GameModeToLocalizedString(MpGameMode mode);
		/** @brief Converts the non-localized string representation back to @ref MpGameMode */
		static MpGameMode StringToGameMode(StringView value);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		using NetworkManagerBase::CreateServer;
#endif

		ConnectionResult OnPeerConnected(const Peer& peer, std::uint32_t clientData) override;
		void OnPeerDisconnected(const Peer& peer, Reason reason) override;

	private:
		std::unique_ptr<ClientConfiguration> _clientConfig;
		std::unique_ptr<ServerConfiguration> _serverConfig;
		std::unique_ptr<ServerDiscovery> _discovery;
		HashMap<Peer, PeerDesc> _peerDesc; // Server: Per peer descriptor

		static void FillServerConfigurationFromFile(StringView path, ServerConfiguration& serverConfig, HashMap<String, bool>& includedFiles, std::int32_t level);
	};
}

#endif