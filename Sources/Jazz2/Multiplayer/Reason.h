#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Main.h"

namespace Jazz2::Multiplayer
{
	/** @brief Client disconnect reason */
	enum class Reason : std::uint32_t
	{
		Unknown,								/**< Unspecified */
		Disconnected,							/**< Client disconnected by user */
		InvalidParameter,						/**< Invalid parameter specified */
		IncompatibleVersion,					/**< Incompatible client version */
		AuthFailed,								/**< Authentication failed */
		InvalidPassword,						/**< Invalid password specified */
		InvalidPlayerName,						/**< Invalid player name specified */
		NotInWhitelist,							/**< Client is not in server whitelist */
		Requires3rdPartyAuthProvider,			/**< Server requires 3rd party authentication provider (e.g., Discord) */
		ServerIsFull,							/**< Server is full or busy */
		ServerNotReady,							/**< Server is not ready yet */
		ServerStopped,							/**< Server is stopped for unknown reason */
		ServerStoppedForMaintenance,			/**< Server is stopped for maintenance */
		ServerStoppedForReconfiguration,		/**< Server is stopped for reconfiguration */
		ServerStoppedForUpdate,					/**< Server is stopped for update */
		ConnectionLost,							/**< Connection lost */
		ConnectionTimedOut,						/**< Connection timed out */
		Kicked,									/**< Kicked by server */
		Banned,									/**< Banned by server */
		CheatingDetected,						/**< Cheating detected */
		AssetStreamingNotAllowed,				/**< Downloading of assets is not allowed, but some assets are missing */
		Idle									/**< Inactivity */
	};
}

#endif