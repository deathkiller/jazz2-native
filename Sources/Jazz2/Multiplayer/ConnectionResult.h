#pragma once

#if defined(WITH_MULTIPLAYER)

#include "Reason.h"

namespace Jazz2::Multiplayer
{
	struct ConnectionResult
	{
		Reason FailureReason;

		ConnectionResult(Reason reason);
		ConnectionResult(bool success);

		bool IsSuccessful() const;
	};
}

#endif