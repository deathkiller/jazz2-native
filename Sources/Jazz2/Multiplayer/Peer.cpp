#include "Peer.h"

#if defined(WITH_MULTIPLAYER)

#if !defined(DEATH_TARGET_EMSCRIPTEN)
#	include "Backends/enet.h"
#endif

namespace Jazz2::Multiplayer
{
	std::uint32_t Peer::GetRoundTripTime() const
	{
#if defined(WITH_WEBSOCKET)
		if (_backend != 0) {
			return 0;
		}
#endif
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		return (_enet != nullptr ? _enet->roundTripTime : 0);
#else
		return 0;
#endif
	}
}

#endif
