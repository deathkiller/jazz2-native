#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Reason.h"

namespace Jazz2::Multiplayer
{
	/** @brief Describes a connection result of @ref INetworkHandler::OnPeerConnected() */
	struct ConnectionResult
	{
		/** @brief Failure reason if the connection was not successful */
		Reason FailureReason;

		/** @brief Creates an instance from a failure reason */
		ConnectionResult(Reason reason);
		/** @brief Creates an instance from a success state */
		ConnectionResult(bool success);

		explicit operator bool() const {
			return IsSuccessful();
		}

		/** @brief Returns `true` if the connection was successful */
		bool IsSuccessful() const;
	};
}

#endif