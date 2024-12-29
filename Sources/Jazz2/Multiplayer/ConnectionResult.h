#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

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