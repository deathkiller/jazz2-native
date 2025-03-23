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
		PlayerKeyPress,
		PlayerChangeWeaponRequest
	};

	/** @brief Packet type going from server to client */
	enum class ServerPacketType
	{
		Null,
		Pong,
		Reserved,

		PeerStateChanged,

		LoadLevel,
		ShowInGameLobby,
		FadeOut,
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

		PlayerSetProperty,
		PlayerRespawn,
		PlayerMoveInstantly,
		PlayerAckWarped,			// TODO
		PlayerActivateForce,		// TODO
		PlayerEmitWeaponFlare,
		PlayerChangeWeapon,
		PlayerTakeDamage,
		PlayerActivateSpring,
		PlayerWarpIn
	};

	/** @brief Player property type from @ref ServerPacketType::PlayerSetProperty */
	enum class PlayerPropertyType
	{
		Unknown,

		Health = 1,
		Controllable,
		Invulnerable,
		Modifier,
		DizzyTime,

		WeaponAmmo = 10,
		WeaponUpgrades,

		Coins = 20,
		Gems,

		Points = 30,
		Deaths,
		Kills,
		Laps,
		TreasureCollected,

		Count
	};
}

#endif