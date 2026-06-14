#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Reason.h"

namespace Jazz2::Multiplayer
{
	/**
		@brief Describes a connection result of @ref INetworkHandler::OnPeerConnected()
		
		Returned by a connection handler to accept or reject an incoming peer. It wraps either a success
		state or a @ref Reason explaining the rejection, and converts implicitly to `bool` for convenience.
	*/
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