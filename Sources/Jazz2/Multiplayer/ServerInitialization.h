#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MpGameMode.h"
#include "../LevelInitialization.h"

#include "../../nCine/Base/HashMap.h"

#include <Containers/SmallVector.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2::Multiplayer
{
	/**
		@brief Playlist entry in @ref ServerConfiguration
	*/
	struct PlaylistEntry
	{
		/** @brief Level name in `<episode>/<level>` format */
		String LevelName;
		/** @brief Game mode */
		MpGameMode GameMode;

		/** @brief Whether reforged gameplay is enabled */
		bool ReforgedGameplay;
		/** @brief Whether every player has limited number of lives, the game ends when only one player remains */
		bool Elimination;
		/** @brief Initial player health, default is unlimited for Race and Treasure Hunt, otherwise 5 */
		std::int32_t InitialPlayerHealth;
		/** @brief Maximum number of seconds for a game */
		std::uint32_t MaxGameTimeSecs;
		/** @brief Duration of pre-game before starting a round */
		std::uint32_t PreGameSecs;
		/** @brief Duration of invulnerability after (re)spawning */
		std::uint32_t SpawnInvulnerableSecs;
		/** @brief Total number of kills, default is 10 (Battle) */
		std::uint32_t TotalKills;
		/** @brief Total number of laps, default is 3 (Race) */
		std::uint32_t TotalLaps;
		/** @brief Total number of treasure collected, default is 60 (Treasure Hunt) */
		std::uint32_t TotalTreasureCollected;
	};

	/**
		@brief Server configuration
		
		This structure contains the server configuration, which is usually loaded from a file and partially also transferred to connected clients.

		@section Multiplayer-ServerConfiguration-format File format description

		The server configuration is read from a JSON file, which may contain the following fields:
		-   @cpp "$include" @ce : @m_span{m-label m-danger m-flat} string @m_endspan Include configuration from another file by path
		    -   If the JSON contains a @cpp "$include" @ce directive, it will load the referenced files recursively, but only once to avoid infinite loops
		-   @cpp "ServerName" @ce : @m_span{m-label m-danger m-flat} string @m_endspan Name of the server
		-   @cpp "ServerAddressOverride" @ce : @m_span{m-label m-danger m-flat} string @m_endspan Address override allows to specify an alternate address
		    -   The address is used only in the public list to be able to connect to the server from the outside
		    -   It is also possible to specify a different port if it is different from the local port
		-   @cpp "ServerPassword" @ce : @m_span{m-label m-danger m-flat} string @m_endspan Password to join the server
		-   @cpp "WelcomeMessage" @ce : @m_span{m-label m-danger m-flat} string @m_endspan Message displayed to players upon joining
		-   @cpp "MaxPlayerCount" @ce : @m_span{m-label m-warning m-flat} integer @m_endspan Maximum number of players allowed to join
		-   @cpp "MinPlayerCount" @ce : @m_span{m-label m-warning m-flat} integer @m_endspan Minimum number of players required to start a round
		-   @cpp "ServerPort" @ce : @m_span{m-label m-warning m-flat} integer @m_endspan UDP port number on which the server runs
		-   @cpp "IsPrivate" @ce : @m_span{m-label m-default m-flat} bool @m_endspan Whether the server is private and hidden in the server list
		-   @cpp "RequiresDiscordAuth" @ce : @m_span{m-label m-default m-flat} bool @m_endspan If `true`, the server requires Discord authentication
		    -   Discord authentication requires a running Discord client
		    -   Supported platforms are Linux, macOS and Windows, players from other platforms won't be able to join
		-   @cpp "AllowedPlayerTypes" @ce : @m_span{m-label m-warning m-flat} integer @m_endspan Bitmask for allowed player types (@cpp 1 @ce - Jazz, @cpp 2 @ce - Spaz, @cpp 4 @ce - Lori)
		-   @cpp "IdleKickTimeSecs" @ce : @m_span{m-label m-warning m-flat} integer @m_endspan Time in seconds after idle players are kicked (default is **never**)
		-   @cpp "AdminUniquePlayerIDs" @ce : @m_span{m-label m-primary m-flat} object @m_endspan Map of admin player IDs
		    -   Key specifies player ID, value contains privileges
		-   @cpp "WhitelistedUniquePlayerIDs" @ce : @m_span{m-label m-primary m-flat} object @m_endspan Map of whitelisted player IDs
		    -   Key specifies player ID, value can contain a user-defined comment
		    -   If at least one entry is specified, only whitelisted players can join the server
		-   @cpp "BannedUniquePlayerIDs" @ce : @m_span{m-label m-primary m-flat} object @m_endspan Map of banned player IDs
		    -   Key specifies player ID, value can contain a user-defined comment (e.g., reason)
		-   @cpp "BannedIPAddresses" @ce : @m_span{m-label m-primary m-flat} object @m_endspan Map of banned IP addresses
		    -   Key specifies IP address, value can contain a user-defined comment (e.g., reason)
		-   @cpp "ReforgedGameplay" @ce : @m_span{m-label m-default m-flat} bool @m_endspan Whether reforged gameplay is enabled
		    -   Has a higher priority than settings of the player
		-   @cpp "RandomizePlaylist" @ce : @m_span{m-label m-default m-flat} bool @m_endspan Whether to play the playlist in random order
		    -   If enabled, the list is shuffled when the server is started and when the end of the list is reached
		-   @cpp "TotalPlayerPoints" @ce : @m_span{m-label m-warning m-flat} integer @m_endspan Total points to win the championship (default is **0**)
		    -   Player can score a maximum of 20 points per round
		-   @cpp "GameMode" @ce : @m_span{m-label m-danger m-flat} string @m_endspan Game mode
		    -   @cpp "b" @ce / @cpp "battle" @ce - Battle
		    -   @cpp "tb" @ce / @cpp "teambattle" @ce - Team Battle
		    -   @cpp "r" @ce / @cpp "race" @ce - Race
		    -   @cpp "tr" @ce / @cpp "teamrace" @ce - Team Race
		    -   @cpp "th" @ce / @cpp "treasurehunt" @ce - Treasure Hunt
		    -   @cpp "tth" @ce / @cpp "teamtreasurehunt" @ce - Team Treasure Hunt
		    -   @cpp "ctf" @ce / @cpp "capturetheflag" @ce - Capture The Flag
		    -   @cpp "c" @ce / @cpp "coop" @ce / @cpp "cooperation" @ce - Cooperation
		-   @cpp "Elimination" @ce : @m_span{m-label m-default m-flat} bool @m_endspan Whether elimination mode is enabled
		    -   If enabled, a player has a limited number of lives given by @cpp "TotalKills" @ce property
		    -   Game ends when only one player remains, or when the conditions of the specified game mode are met
		    -   Elimination can be combined with any game mode
		-   @cpp "InitialPlayerHealth" @ce : @m_span{m-label m-warning m-flat} integer @m_endspan Initial health of players
			-   If the property is not specified or is less than 1, the player's health is automatically assigned depending on the game mode
			-   Default value for Race and Treasure Hunt is **unlimited**, in all other game modes it's **5**
		-   @cpp "MaxGameTimeSecs" @ce : @m_span{m-label m-warning m-flat} integer @m_endspan Maximum allowed game time in seconds per level (default is **unlimited**)
		-   @cpp "PreGameSecs" @ce : @m_span{m-label m-warning m-flat} integer @m_endspan Pre-game duration in seconds (default is **60** seconds)
		    -   Pre-game is skipped in Cooperation
		-   @cpp "SpawnInvulnerableSecs" @ce : @m_span{m-label m-warning m-flat} integer @m_endspan Duration of invulnerability after (re)spawning (default is **4** seconds)
		    -   Invulnerability is skipped in Cooperation
		-   @cpp "TotalKills" @ce : @m_span{m-label m-warning m-flat} integer @m_endspan Number of kills required to win (Battle)
		-   @cpp "TotalLaps" @ce : @m_span{m-label m-warning m-flat} integer @m_endspan Number of laps required to win (Race)
		-   @cpp "TotalTreasureCollected" @ce : @m_span{m-label m-warning m-flat} integer @m_endspan Number of treasures required to win (Treasure Hunt)
		-   @cpp "Playlist" @ce : @m_span{m-label m-success m-flat} array @m_endspan List of game configurations per round, each entry may contain:
		    -   @cpp "LevelName" @ce : @m_span{m-label m-danger m-flat} string @m_endspan Name of the level in `<episode>/<level>` format
		    -   @cpp "GameMode" @ce : @m_span{m-label m-danger m-flat} string @m_endspan Specific game mode for this round
		    -   @cpp "Elimination" @ce : @m_span{m-label m-default m-flat} bool @m_endspan Whether elimination mode is enabled for this round
		    -   @cpp "InitialPlayerHealth" @ce : @m_span{m-label m-warning m-flat} integer @m_endspan Initial health of players for this round
		    -   @cpp "MaxGameTimeSecs" @ce : @m_span{m-label m-warning m-flat} integer @m_endspan Maximum game duration for this round
		    -   @cpp "PreGameSecs" @ce : @m_span{m-label m-warning m-flat} integer @m_endspan Pre-game duration before this round starts
		    -   @cpp "SpawnInvulnerableSecs" @ce : @m_span{m-label m-warning m-flat} integer @m_endspan Duration of invulnerability after (re)spawning
		    -   @cpp "TotalKills" @ce : @m_span{m-label m-warning m-flat} integer @m_endspan Number of kills required to win this round (Battle)
		    -   @cpp "TotalLaps" @ce : @m_span{m-label m-warning m-flat} integer @m_endspan Number of laps required to win this round (Race)
		    -   @cpp "TotalTreasureCollected" @ce : @m_span{m-label m-warning m-flat} integer @m_endspan Total treasure required to win this round (Treasure Hunt)
		-   @cpp "PlaylistIndex" @ce : @m_span{m-label m-warning m-flat} integer @m_endspan Index of the current playlist entry
		
		If a property is missing in a playlist entry, it will inherit the value from the root configuration.
		If a property is missing in the root configuration, the default value is used. `{PlayerName}` and
		`{ServerName}` variables can be used in @cpp "ServerName" @ce and @cpp "WelcomeMessage" @ce properties.
		Both properties also support @ref Jazz2-UI-Font-format "text formatting" using the @cpp "\f[…]" @ce notation.
		
		@subsection Multiplayer-ServerConfiguration-format-example Example server configuration
		
		@include ServerConfiguration.json
	*/
	struct ServerConfiguration
	{
		/** @{ @name Server settings */

		/** @brief Server name */
		String ServerName;
		/** @brief Server address override allows to specify an alternate address under which the server will be listed */
		String ServerAddressOverride;
		/** @brief Password of the server */
		String ServerPassword;
		/** @brief Welcome message displayed to players upon joining */
		String WelcomeMessage;
		/** @brief Maximum number of players allowed to join */
		std::uint32_t MaxPlayerCount;
		/** @brief Minimum number of players to start a round */
		std::uint32_t MinPlayerCount;
		/** @brief Game mode */
		MpGameMode GameMode;
		/** @brief Server port */
		std::uint16_t ServerPort;
		/** @brief Whether the server is private (i.e. not visible in the server list) */
		bool IsPrivate;
		/** @brief Whether Discord authentication is required to join the server */
		bool RequiresDiscordAuth;
		/** @brief Allowed player types as bitmask of @ref PlayerType */
		std::uint8_t AllowedPlayerTypes;
		/** @brief Time after which inactive players will be kicked, in seconds, -1 to disable */
		std::int32_t IdleKickTimeSecs;
		/** @brief List of unique player IDs with admin rights, value contains list of privileges, or `*` for all privileges */
		HashMap<String, String> AdminUniquePlayerIDs;
		/** @brief List of whitelisted unique player IDs, value can contain user-defined comment */
		HashMap<String, String> WhitelistedUniquePlayerIDs;
		/** @brief List of banned unique player IDs, value can contain user-defined reason */
		HashMap<String, String> BannedUniquePlayerIDs;
		/** @brief List of banned IP addresses, value can contain user-defined reason */
		HashMap<String, String> BannedIPAddresses;

		/** @} */

		/** @{ @name Game-specific settings */

		/** @brief Whether reforged gameplay is enabled, see @ref PreferencesCache::EnableReforgedGameplay */
		bool ReforgedGameplay;
		/** @brief Whether to play the playlist in random order */
		bool RandomizePlaylist;
		/** @brief Whether every player has limited number of lives, the game ends when only one player remains */
		bool Elimination;
		/** @brief Total player points to win the championship */
		std::uint32_t TotalPlayerPoints;
		/** @brief Initial player health, default is unlimited for Race and Treasure Hunt, otherwise 5 */
		std::int32_t InitialPlayerHealth;
		/** @brief Maximum number of seconds for a game */
		std::uint32_t MaxGameTimeSecs;
		/** @brief Duration of pre-game before starting a round */
		std::uint32_t PreGameSecs;
		/** @brief Duration of invulnerability after (re)spawning */
		std::uint32_t SpawnInvulnerableSecs;
		/** @brief Total number of kills, default is 10 (Battle) */
		std::uint32_t TotalKills;
		/** @brief Total number of laps, default is 3 (Race) */
		std::uint32_t TotalLaps;
		/** @brief Total number of treasure collected, default is 60 (Treasure Hunt) */
		std::uint32_t TotalTreasureCollected;

		/** @brief Index of the current playlist entry */
		std::int32_t PlaylistIndex;
		/** @brief Playlist */
		SmallVector<PlaylistEntry, 0> Playlist;

		/** @} */

		/** @{ @name Pure runtime information */

		/** @brief Path to the configuration file */
		String FilePath;

		/** @brief Start time of the server as Unix timestamp */
		std::uint64_t StartUnixTimestamp;

		/** @} */
	};

	/**
		@brief Server initialization parameters
	*/
	struct ServerInitialization
	{
		/** @brief Server configuration */
		ServerConfiguration Configuration;
		/** @brief Level initialization parameters */
		LevelInitialization InitialLevel;
	};
}

#endif