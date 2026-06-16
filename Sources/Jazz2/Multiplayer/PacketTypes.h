#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Main.h"

namespace Jazz2::Multiplayer
{
	/**
		@brief Packet type broadcasted on the local network
		
		Identifies a connectionless packet exchanged over the LAN discovery channel, used by clients to
		locate running servers and by servers to announce themselves in response.
	*/
	enum class BroadcastPacketType
	{
		Null,				/**< Empty packet */
		DiscoveryRequest,	/**< Request to discover servers on the local network */
		DiscoveryResponse	/**< Response of a server to a discovery request */
	};

	/**
		@brief Packet type going from client to server
		
		Identifies the kind of message sent in the client-to-server direction, covering connection setup
		(ping, authentication), level and chat handshakes, and periodic player state and input updates.
	*/
	enum class ClientPacketType
	{
		Null,						/**< Empty packet */
		Ping,						/**< Ping request to measure round trip time */
		Reserved,					/**< Reserved */
		Rpc,						/**< Remote procedure call forwarded to a scripted actor */

		Auth = 10,					/**< Authentication request */
		LevelReady,					/**< Notifies the server that the level finished loading */
		ChatMessage,				/**< Chat message sent to the server */
		ValidateAssetsResponse,		/**< Response to a server request to validate required assets */

		ForceResyncActors = 20,		/**< Requests the server to resynchronize all actors */

		PlayerReady = 30,			/**< Notifies the server that the player is ready to spawn */
		PlayerUpdate,				/**< Periodic update of the local player state */
		PlayerKeyPress,				/**< Player input state */
		PlayerChangeWeaponRequest,	/**< Requests a weapon change */
		PlayerSpectateRequest,		/**< Requests to enable or disable spectate mode */
		PlayerAckWarped,			/**< Acknowledges that the player has been warped */
		PlayerChangeCharacter,		/**< Requests to change the player character */
		PlayerChangeTeamRequest		/**< Requests to change the player team */
	};

	/**
		@brief Packet type going from server to client
		
		Identifies the kind of message sent in the server-to-client direction, covering authentication and
		asset handshakes, level loading and synchronization, actor creation and updates, and player state
		changes pushed by the server.
	*/
	enum class ServerPacketType
	{
		Null,							/**< Empty packet */
		Pong,							/**< Response to a ping request */
		Reserved,						/**< Reserved */
		Rpc,							/**< Remote procedure call forwarded to a scripted actor */

		AuthResponse = 70,				/**< Response to an authentication request */
		PeerSetProperty,				/**< Sets a property of a peer */
		ValidateAssets,					/**< Requests the client to validate its required assets */
		StreamAsset,					/**< Streams a missing asset to the client */

		LoadLevel = 80,					/**< Requests the client to load a level */
		LevelSetProperty,				/**< Sets a property of the current level */
		LevelResetProperties,		// TODO
		ShowInGameLobby,				/**< Requests the client to show the in-game lobby */
		FadeOut,						/**< Requests the client to fade out the screen */
		PlaySfx,						/**< Plays a sound effect attached to an actor */
		PlayCommonSfx,					/**< Plays a common sound effect at a position */
		ShowAlert,					// TODO
		ChatMessage,					/**< Chat message broadcasted to the client */
		SyncTileMap,					/**< Synchronizes the state of the tile map */
		SetTrigger,						/**< Sets the state of a trigger */
		AdvanceTileAnimation,			/**< Advances a destructible tile animation */
		RevertTileAnimation,		// TODO
		CreateDebris,					/**< Creates particle or sprite debris */

		CreateControllablePlayer = 110,	/**< Creates a player controllable by the client */
		CreateRemoteActor,				/**< Creates a remote actor on the client */
		CreateMirroredActor,			/**< Creates a mirrored actor on the client */
		DestroyRemoteActor,				/**< Destroys a remote actor on the client */
		UpdateAllActors,				/**< Periodic update of all remote actors */
		ChangeRemoteActorMetadata,		/**< Changes metadata of a remote actor */
		MarkRemoteActorAsPlayer,		/**< Marks a remote actor as another player */
		UpdatePositionsInRound,			/**< Updates player positions in the current round */
		SyncRaceCheckpoints,			/**< Sends the ordered race checkpoint polyline and start markers for the minimap */
		SyncTeamScores,					/**< Sends the per-team aggregate scores for the HUD */
		SyncScoreboard,					/**< Sends per-player scoreboard rows (name, kills, deaths, points, ping) */

		PlayerSetProperty = 130,		/**< Sets a property of a player */
		PlayerResetProperties,			/**< Resets all properties of a player */
		PlayerRespawn,					/**< Respawns a player */
		PlayerMoveInstantly,			/**< Moves a player instantly to a position */
		PlayerAckWarped,			// TODO
		PlayerActivateForce,		// TODO
		PlayerEmitWeaponFlare,			/**< Emits a weapon flare from a player */
		PlayerChangeWeapon,				/**< Changes the current weapon of a player */
		PlayerTakeDamage,				/**< Applies damage to a player */
		PlayerPush,						/**< Pushes a player */
		PlayerActivateSpring,			/**< Activates a spring under a player */
		PlayerWarpIn					/**< Warps a player into the level */
	};

	/**
		@brief Peer property type from @ref ServerPacketType::PeerSetProperty
		
		Identifies which per-peer session property the server is announcing to clients, such as a peer
		connecting, disconnecting or being roasted.
	*/
	enum class PeerPropertyType
	{
		Unknown,		/**< Unknown */

		Connected,		/**< Peer connected */
		Disconnected,	/**< Peer disconnected */
		Roasted,		/**< Peer was roasted (killed) */

		Count			/**< Count of supported property types */
	};

	/**
		@brief Level property type from @ref ServerPacketType::LevelSetProperty
		
		Identifies which property of the current level the server is updating on clients, such as the level
		state, the active game mode or the currently playing music.
	*/
	enum class LevelPropertyType
	{
		Unknown,			/**< Unknown */

		State = 1,			/**< Level state */
		GameMode,			/**< Game mode */

		LevelText = 10,		// TODO
		Music,				/**< Currently playing music */

		Count				/**< Count of supported property types */
	};

	/**
		@brief Player property type from @ref ServerPacketType::PlayerSetProperty
		
		Identifies which property of a player the server is updating on clients, covering character and
		health state, weapon ammo and upgrades, collected currency, and per-round statistics.
	*/
	enum class PlayerPropertyType
	{
		Unknown,			/**< Unknown */

		PlayerType = 1,		/**< Player type (character) */
		Lives,				/**< Remaining lives */
		Health,				/**< Current health */
		Controllable,		/**< Whether the player is controllable */
		Invulnerable,		/**< Invulnerability state */
		Modifier,			/**< Active modifier (e.g. copter, frog) */
		Dizzy,				/**< Dizziness state */
		Freeze,				/**< Freeze state */
		Shield,				/**< Active shield */
		LimitCameraView,	/**< Camera view limit */
		OverrideCameraView,	/**< Camera view override */
		ShakeCameraView,	/**< Camera shake */
		Spectate,			/**< Spectate mode state */
		Team,				/**< Team the player belongs to */

		WeaponAmmo = 30,	/**< Ammo of a weapon */
		WeaponUpgrades,		/**< Upgrades of a weapon */

		Coins = 60,			/**< Collected coins */
		Gems,				/**< Collected gems */
		Score,				/**< Current score */

		Points = 90,		/**< Earned points in the session */
		PositionInRound,	// TODO: Unused
		Deaths,				/**< Deaths in the current round */
		Kills,				/**< Kills in the current round */
		Laps,				/**< Completed laps in the current round */
		TreasureCollected,	/**< Treasure collected in the current round */

		Count				/**< Count of supported property types */
	};
}

#endif