#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Reason.h"

namespace Jazz2::Multiplayer
{
	/** @brief Describes a connection result of @ref INetworkHandler::OnPeerConnected() */
	struct ConnectionResult
	{
		Reason FailureReason;

		ConnectionResult(Reason reason);
		ConnectionResult(bool success);

		bool IsSuccessful() const;
	};
}

#endif