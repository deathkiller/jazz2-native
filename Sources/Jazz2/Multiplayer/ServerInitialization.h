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

		/** @brief Whether every player has limited number of lives, the game ends when only one player remains */
		bool IsElimination;
		/** @brief Initial player health, default is 5 */
		std::uint32_t InitialPlayerHealth;
		/** @brief Maximum number of seconds for a game */
		std::uint32_t MaxGameTimeSecs;
		/** @brief Duration of pre-game before starting the actual game */
		std::uint32_t PreGameSecs;
		/** @brief Total number of kills, default is 10 (Battle) */
		std::uint32_t TotalKills;
		/** @brief Total number of laps, default is 3 (Race) */
		std::uint32_t TotalLaps;
		/** @brief Total number of treasure collected, default is 100 (Treasure Hunt) */
		std::uint32_t TotalTreasureCollected;
	};

	/**
		@brief Server configuration
	*/
	struct ServerConfiguration
	{
		/** @brief Server name */
		String ServerName;
		/** @brief Password of the server */
		String ServerPassword;
		/** @brief Welcome message in the lobby */
		String WelcomeMessage;
		/** @brief Maximum number of players allowed on the server */
		std::uint32_t MaxPlayerCount;
		/** @brief Minimum number of players to start the game */
		std::uint32_t MinPlayerCount;
		/** @brief Multiplayer game mode */
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

		// Game mode specific settings
		/** @brief Whether to play the playlist in random order */
		bool RandomizePlaylist;
		/** @brief Whether every player has limited number of lives, the game ends when only one player remains */
		bool IsElimination;
		/** @brief Total player points to win the championship, default is 50 */
		std::uint32_t TotalPlayerPoints;
		/** @brief Initial player health, default is 5 */
		std::uint32_t InitialPlayerHealth;
		/** @brief Maximum number of seconds for a game */
		std::uint32_t MaxGameTimeSecs;
		/** @brief Duration of pre-game before starting the actual game (ignored in Cooperation) */
		std::uint32_t PreGameSecs;
		/** @brief Total number of kills, default is 10 (Battle) */
		std::uint32_t TotalKills;
		/** @brief Total number of laps, default is 3 (Race) */
		std::uint32_t TotalLaps;
		/** @brief Total number of treasure collected, default is 100 (Treasure Hunt) */
		std::uint32_t TotalTreasureCollected;

		/** @brief Playlist */
		SmallVector<PlaylistEntry, 0> Playlist;
		/** @brief Index of the current playlist entry, or -1 to disable playlist */
		std::int32_t PlaylistIndex;

		// Pure runtime information
		/** @brief Start time of the server as Unix timestamp */
		std::uint64_t StartUnixTimestamp;
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