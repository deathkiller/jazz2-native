#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Main.h"

namespace Jazz2::Multiplayer
{
	/** @brief Packet type broadcasted on the local network */
	enum class BroadcastPacketType
	{
		Null,
		DiscoveryRequest,
		DiscoveryResponse
	};

	/** @brief Packet type going from client to server */
	enum class ClientPacketType
	{
		Null,
		Ping,
		Reserved,

		Auth,
		LevelReady,
		ChatMessage,

		PlayerReady,
		PlayerUpdate,
		PlayerKeyPress
	};

	/** @brief Packet type going from server to client */
	enum class ServerPacketType
	{
		Null,
		Pong,
		Reserved,

		LoadLevel,
		ShowInGameLobby,
		ChangeGameMode,
		PlaySfx,
		PlayCommonSfx,
		ShowAlert,					// TODO
		OverrideLevelText,			// TODO
		ChatMessage,
		SyncTileMap,
		SetTrigger,
		AdvanceTileAnimation,
		RevertTileAnimation,		// TODO

		CreateControllablePlayer,
		CreateRemoteActor,
		CreateMirroredActor,
		DestroyRemoteActor,
		UpdateAllActors,
		MarkRemoteActorAsPlayer,

		PlayerRespawn,
		PlayerMoveInstantly,
		PlayerAckWarped,			// TODO
		PlayerActivateForce,		// TODO
		PlayerAddHealth,			// TODO
		PlayerEmitWeaponFlare,
		PlayerChangeWeapon,
		PlayerRefreshAmmo,
		PlayerRefreshWeaponUpgrades,
		PlayerRefreshCoins,
		PlayerRefreshGems,
		PlayerSetControllable,		// TODO
		PlayerSetDizzyTime,			// TODO
		PlayerSetInvulnerability,	// TODO
		PlayerSetLaps,				// TODO
		PlayerSetModifier,			// TODO
		PlayerSetStats,				// TODO
		PlayerTakeDamage,
		PlayerActivateSpring,
		PlayerWarpIn
	};
}

#endif