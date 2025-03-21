﻿#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MpGameMode.h"
#include "../LevelInitialization.h"

#include "../../nCine/Base/HashMap.h"

using namespace nCine;

namespace Jazz2::Multiplayer
{
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
		/** @brief Maximum number of players */
		std::uint32_t MaxPlayerCount;
		/** @brief Multiplayer game mode */
		MpGameMode GameMode;
		/** @brief Server port */
		std::uint16_t ServerPort;
		/** @brief Whether the server is private (i.e. not visible in the server list) */
		bool IsPrivate;
		/** @brief Allowed player types as bitmask of @ref PlayerType */
		std::uint8_t AllowedPlayerTypes;
		/** @brief Time after which inactive players will be kicked, in seconds, `-1` to disable */
		std::int32_t IdleKickTimeSecs;
		/** @brief List of unique player IDs with admin rights, value contains list of privileges, or `*` for all privileges */
		HashMap<String, String> AdminUniquePlayerIDs;
		/** @brief List of whitelisted unique player IDs, value can contain user-defined comment */
		HashMap<String, String> WhitelistedUniquePlayerIDs;
		/** @brief List of banned unique player IDs, value can contain user-defined reason */
		HashMap<String, String> BannedUniquePlayerIDs;
		/** @brief List of banned IP addresses, value can contain user-defined reason */
		HashMap<String, String> BannedIPAddresses;
		/** @brief Whether Discord authentication is required to join the server */
		bool RequiresDiscordAuth;
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