#include "Peer.h"

#if defined(WITH_MULTIPLAYER)

#if !defined(DEATH_TARGET_EMSCRIPTEN)
#	include "Backends/enet.h"
#endif

namespace Jazz2::Multiplayer
{
	std::uint32_t Peer::GetRoundTripTime() const
	{
		// WebSocket transport does not provide round trip time information
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		return (_enet != nullptr ? _enet->roundTripTime : 0);
#else
		return 0;
#endif
	}
}

namespace Death::Implementation
{
	std::size_t Formatter<Jazz2::Multiplayer::Peer>::format(const Containers::MutableStringView& buffer, const Jazz2::Multiplayer::Peer& value, FormatContext& context)
	{
#if defined(WITH_WEBSOCKET)
		if DEATH_UNLIKELY(value.IsWebSocket()) {
			return formatInto(buffer, "W|{:.6x}", value.GetId());
		}
#endif
		return formatInto(buffer, "{:.8x}", value.GetId());
	}
}

#endif
