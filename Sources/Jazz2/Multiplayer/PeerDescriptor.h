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
		@brief Server-side anti-cheat state of a peer

		Both the client and the server simulate the player, so their views of it always deviate a little (latency,
		jitter, a different frame rate), and a single implausible update is not evidence of cheating. Violations
		therefore only accumulate a suspicion score, which decays over time, and the server escalates in stages
		based on it --- see @ref MpLevelHandler for the thresholds. Owned by the main thread.
	*/
	struct AntiCheatState
	{
		/** @brief Accumulated suspicion, decays over time */
		float Suspicion = 0.0f;
		/** @brief Server time in milliseconds when @ref Suspicion was last decayed */
		std::uint64_t SuspicionUpdatedMs = 0;
		/** @brief Server time in milliseconds when the current incident started, i.e., when @ref Suspicion rose from zero */
		std::uint64_t IncidentStartMs = 0;
		/** @brief Number of violations in the current incident */
		std::uint32_t IncidentViolations = 0;
		/** @brief Server time in milliseconds when the server console was last notified about this peer */
		std::uint64_t NotifiedMs = 0;
		/** @brief Whether the server console was already notified in the current incident */
		bool Notified = false;
		/** @brief Whether the peer was already kicked, so it's not kicked again by updates still in flight */
		bool Kicked = false;
		/** @brief Server time in milliseconds when an implausible speed was last counted (it's reported in every update) */
		std::uint64_t SpeedViolationMs = 0;

		/** @brief Last position reported by the client that was accepted */
		Vector2f LastAcceptedPos;
		/** @brief Whether @ref LastAcceptedPos is valid */
		bool HasLastAcceptedPos = false;
		/** @brief Remaining movement budget in pixels, refilled with the server time */
		float MoveBudget = 0.0f;
		/** @brief Server time in milliseconds when @ref MoveBudget was last refilled, @cpp 0 @ce to refill it completely */
		std::uint64_t MoveBudgetUpdatedMs = 0;

		/** @brief Server time in milliseconds when the current clock-drift window started, @cpp 0 @ce if not started yet */
		std::uint64_t ClockWindowStartMs = 0;
		/** @brief Minimal difference between the server and the client time in the current window */
		std::int64_t ClockWindowMinOffset = INT64_MAX;
		/** @brief Minimal difference between the server and the client time in the previous window, @cpp INT64_MAX @ce if none */
		std::int64_t ClockPrevMinOffset = INT64_MAX;

		/** @brief Forgets the movement history, the next update is checked only against the server position */
		void ResetMovement() {
			HasLastAcceptedPos = false;
			MoveBudgetUpdatedMs = 0;
		}

		/** @brief Forgets the movement history and anchors it to the specified position with a full budget */
		void ResetMovement(Vector2f pos) {
			LastAcceptedPos = pos;
			HasLastAcceptedPos = true;
			MoveBudgetUpdatedMs = 0;
		}
	};

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
		/** @brief Time when the peer connected, a peer that doesn't authenticate in time is kicked */
		TimeStamp ConnectedSince;
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

		/** @brief Anti-cheat state (server only) */
		AntiCheatState AntiCheat;

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