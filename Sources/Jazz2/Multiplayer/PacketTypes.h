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
		Rpc,

		Auth = 10,
		LevelReady,
		ChatMessage,
		ValidateAssetsResponse,

		ForceResyncActors = 20,

		PlayerReady = 30,
		PlayerUpdate,
		PlayerKeyPress,
		PlayerChangeWeaponRequest,
		PlayerSpectateRequest,
		PlayerAckWarped
	};

	/** @brief Packet type going from server to client */
	enum class ServerPacketType
	{
		Null,
		Pong,
		Reserved,
		Rpc,

		AuthResponse = 70,
		PeerSetProperty,
		ValidateAssets,
		StreamAsset,

		LoadLevel = 80,
		LevelSetProperty,
		LevelResetProperties,		// TODO
		ShowInGameLobby,
		FadeOut,
		PlaySfx,
		PlayCommonSfx,
		ShowAlert,					// TODO
		ChatMessage,
		SyncTileMap,
		SetTrigger,
		AdvanceTileAnimation,
		RevertTileAnimation,		// TODO
		CreateDebris,

		CreateControllablePlayer = 110,
		CreateRemoteActor,
		CreateMirroredActor,
		DestroyRemoteActor,
		UpdateAllActors,
		ChangeRemoteActorMetadata,
		MarkRemoteActorAsPlayer,
		UpdatePositionsInRound,

		PlayerSetProperty = 130,
		PlayerResetProperties,
		PlayerRespawn,
		PlayerMoveInstantly,
		PlayerAckWarped,			// TODO
		PlayerActivateForce,		// TODO
		PlayerEmitWeaponFlare,
		PlayerChangeWeapon,
		PlayerTakeDamage,
		PlayerPush,
		PlayerActivateSpring,
		PlayerWarpIn
	};

	/** @brief Peer property type from @ref ServerPacketType::PeerSetProperty */
	enum class PeerPropertyType
	{
		Unknown,

		Connected,
		Disconnected,
		Roasted,

		Count
	};

	/** @brief Level property type from @ref ServerPacketType::LevelSetProperty */
	enum class LevelPropertyType
	{
		Unknown,

		State = 1,
		GameMode,
		
		LevelText = 10,		// TODO
		Music,

		Count
	};

	/** @brief Player property type from @ref ServerPacketType::PlayerSetProperty */
	enum class PlayerPropertyType
	{
		Unknown,

		PlayerType = 1,
		Lives,
		Health,
		Controllable,
		Invulnerable,
		Modifier,
		Dizzy,
		Freeze,
		Shield,
		LimitCameraView,
		OverrideCameraView,
		ShakeCameraView,
		Spectate,

		WeaponAmmo = 30,
		WeaponUpgrades,

		Coins = 60,
		Gems,
		Score,

		Points = 90,
		PositionInRound,	// TODO: Unused
		Deaths,
		Kills,
		Laps,
		TreasureCollected,

		Count
	};
}

#endif