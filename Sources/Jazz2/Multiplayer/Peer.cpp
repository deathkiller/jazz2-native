#include "Peer.h"

#if defined(WITH_MULTIPLAYER)

#include "NetworkManagerBase.h"

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
