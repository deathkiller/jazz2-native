#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Main.h"

namespace Jazz2::Multiplayer
{
	/** @brief Client disconnect reason */
	enum class Reason : std::uint32_t
	{
		Unknown,
		Disconnected,
		IncompatibleVersion,
		ServerIsFull,
		ServerNotReady,
		ServerStopped,
		ServerStoppedForMaintenance,
		ServerStoppedForReconfiguration,
		ServerStoppedForUpdate,
		ConnectionLost,
		ConnectionTimedOut,
		Kicked,
		Banned
	};
}

#endif