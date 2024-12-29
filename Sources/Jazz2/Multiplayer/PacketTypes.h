#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Main.h"

namespace Jazz2::Multiplayer
{
	enum class BroadcastPacketType
	{
		Null,
		DiscoveryRequest,
		DiscoveryResponse
	};

	enum class ClientPacketType
	{
		Null,
		Ping,
		Reserved,

		Auth,
		LevelReady,
		ChatMessage,

		PlayerUpdate,
		PlayerKeyPress
	};

	enum class ServerPacketType
	{
		Null,
		Pong,
		Reserved,

		LoadLevel,
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