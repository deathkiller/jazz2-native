#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Peer.h"
#include "../PlayerType.h"
#include "../../nCine/Base/TimeStamp.h"

#include <Containers/StaticArray.h>
#include <Containers/String.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2::Actors::Multiplayer
{
	class MpPlayer;
}

namespace Jazz2::Multiplayer
{
	/** @brief Peer state in a level  */
	enum class PeerLevelState
	{
		Unknown,			/**< Unknown */
		LevelLoaded,		/**< Peer finished loading of the level */
		LevelSynchronized,	/**< Peer finished synchronized entities in the level */
		PlayerReady,		/**< Player is ready to spawn */
		PlayerSpawned		/**< Player is spawned */
	};

	/** @brief Peer descriptor */
	struct PeerDescriptor
	{
		/** @brief Remote peer */
		Peer RemotePeer;

		/** @brief Unique Player ID */
		StaticArray<16, std::uint8_t> Uuid;
		/** @brief Whether the peer is already successfully authenticated */
		bool IsAuthenticated;
		/** @brief Whether the peer has admin rights */
		bool IsAdmin;
		/** @brief Preferred player type selected by the peer */
		PlayerType PreferredPlayerType;
		/** @brief Player display name */
		String PlayerName;
		/** @brief Earned points in the current session (championship) */
		std::uint32_t Points;

		/** @brief Spawned player */
		Actors::Multiplayer::MpPlayer* Player;
		/** @brief State of the player in the current level */
		PeerLevelState LevelState;
		/** @brief Last update of the player from client */
		std::uint64_t LastUpdated;
		/** @brief Whether ledge climbing is enabled by client */
		bool EnableLedgeClimb;
		/** @brief Team ID */
		std::uint8_t Team;

		/** @brief Deaths of the player in the current round */
		std::uint32_t Deaths;
		/** @brief Kills of the player in the current round */
		std::uint32_t Kills;
		/** @brief Laps of the player in the current round */
		std::uint32_t Laps;
		/** @brief Timestamp when the last lap started */
		TimeStamp LapStarted;
		/** @brief Treasure collected of the player in the current round */
		std::uint32_t TreasureCollected;

		/** @brief Elapsed frames when the player lost all lives */
		float DeathElapsedFrames;

		PeerDescriptor();
	};
}

#endif