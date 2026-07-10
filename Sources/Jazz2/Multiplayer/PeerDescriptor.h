#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Peer.h"
#include "GameModes/MpPlayerState.h"
#include "../LevelInitialization.h"
#include "../PlayerType.h"
#include "../PreferencesCache.h"
#include "../../nCine/Base/TimeStamp.h"

#include <Containers/String.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2::Actors::Multiplayer
{
	class MpPlayer;
}

namespace Jazz2::Multiplayer
{
	/**
		@brief Peer state in a level
		
		Tracks how far a peer has progressed through joining the current level, from validating and
		streaming required assets through loading and synchronization to spectating or having a spawned
		player. The server advances this state as the join handshake completes.
	*/
	enum class PeerLevelState
	{
		Unknown,				/**< Unknown */
		ValidatingAssets,		/**< Peer received list of required assets, the server is waiting for response */
		StreamingMissingAssets,	/**< Missing assets are being streamed to peer */
		LevelLoaded,			/**< Peer finished loading of the level */
		LevelSynchronized,		/**< Peer finished synchronized entities in the level */
		Spectating,				/**< Peer is spectating */
		PlayerReady,			/**< Player is ready to spawn */
		PlayerSpawned			/**< Player is spawned */
	};

	/**
		@brief Spectate mode flags
		
		Describes whether and how a player is spectating instead of playing. The low bits hold the base mode
		(not spectating, forced by the server, or requested by the client), while the @ref SpectateMode::FreeCamera
		flag selects a free-roaming camera over a player-following one.
	*/
	enum class SpectateMode : std::uint8_t {
		None,					/**< Not spectating */
		Forced,					/**< Spectate mode is forced by the server, the client cannot disable it */
		Requested,				/**< Spectate mode is requested by the client, the server can accept or reject it */

		FreeCamera = 0x10,		/**< Spectate with free camera, otherwise spectate with player camera */

		Mask = 0x0F				/**< Mask for spectate mode flags */
	};

	DEATH_ENUM_FLAGS(SpectateMode);

	/**
	 * @brief Peer descriptor
	 *
	 * Holds all per-peer session state tracked by @ref NetworkManager and @ref MpLevelHandler: identity and
	 * authentication, chosen player type/name, the spawned player actor and the carry-over state retained so a
	 * reconnecting player can resume. The per-round game-mode statistics and team assignment are inherited from
	 * @ref MpPlayerState, so the game modes can read and write them through @ref IGameModeContext without knowing
	 * about peers or networking.
	 */
	struct PeerDescriptor : public MpPlayerState
	{
		/** @brief Remote peer if the peer is connected remotely */
		Peer RemotePeer;

		/** @brief Unique Player ID if the peer is connected remotely */
		Uuid UniquePlayerID;
		/** @brief Whether the peer is already successfully authenticated */
		bool IsAuthenticated;
		/** @brief Whether the peer has admin privileges */
		bool IsAdmin;
		/** @brief Whether ledge climbing is enabled by client */
		bool EnableLedgeClimb;
		/** @brief Whether the peer voted "yes" in the active poll */
		bool VotedYes;
		/** @brief Preferred player type selected by the peer */
		PlayerType PreferredPlayerType;
		/** @brief Player display name */
		String PlayerName;
		/** @brief Player character recolor (packed 4-byte fur color; 0 = default colors), as chosen by the peer */
		std::uint32_t FurColor;
		/** @brief Earned points in the current session (championship) */
		std::uint32_t Points;
		/** @brief State of the player in the current level */
		PeerLevelState LevelState;
		/** @brief Spawned player in the current level */
		Actors::Multiplayer::MpPlayer* Player;
		/** @brief Last update of the player from client */
		std::uint64_t LastUpdated;

		/** @brief Start of the current inbound packet-rate window in milliseconds (server-side flood mitigation) */
		std::uint64_t PacketRateWindowStart = 0;
		/** @brief Number of packets received from this peer within the current rate window */
		std::uint32_t PacketRateCount = 0;

		/** @brief Elapsed frames when the player is idle */
		float IdleElapsedFrames;
		/** @brief Time remaining for join cooldown */
		float JoinCooldownFrames;

		/** @brief Whether the player is in spectate mode */
		SpectateMode IsSpectating;

		/** @brief Player state (weapons, lives, score, ...) to apply on the next (re)spawn, see @ref HasCarryOver */
		PlayerCarryOver CarryOver;
		/** @brief Whether @ref CarryOver holds state to apply on the next spawn (level change or reconnect) */
		bool HasCarryOver;
		/** @brief Time when the peer disconnected, used for the reconnect window (invalid while connected) */
		TimeStamp DisconnectedSince;

		/** @brief Creates a new instance */
		PeerDescriptor();
	};
}

#endif