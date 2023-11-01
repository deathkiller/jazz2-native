#include "ConnectionResult.h"

#if defined(WITH_MULTIPLAYER)

#include "../../Common.h"

namespace Jazz2::Multiplayer
{
	ConnectionResult::ConnectionResult(Reason reason)
		: FailureReason(reason)
	{
	}

	ConnectionResult::ConnectionResult(bool success)
		: FailureReason(success ? (Reason)UINT32_MAX : Reason::Unknown)
	{
	}

	bool ConnectionResult::IsSuccessful() const
	{
		return FailureReason == (Reason)UINT32_MAX;
	}
}

#endif