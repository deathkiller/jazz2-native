#pragma once

#if defined(WITH_MULTIPLAYER)

#include "../../Common.h"

namespace Jazz2::Multiplayer
{
	enum class Reason : std::uint32_t
	{
		Unknown,
		Disconnected,
		IncompatibleVersion,
		ServerIsFull,
		ServerNotReady,
		ServerStopped,
		ConnectionLost,
		ConnectionTimedOut,
		Kicked,
		Banned
	};
}

#endif