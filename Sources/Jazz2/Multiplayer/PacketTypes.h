#pragma once

#if defined(WITH_MULTIPLAYER)

#include "../../Common.h"

namespace Jazz2::Multiplayer
{
	enum class ClientPacketType
	{
		Null,
		Ping,
		Reserved,

		Auth,
		LevelReady,

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
		ShowMessage,				// TODO
		OverrideLevelText,			// TODO
		SyncTileMap,
		SetTrigger,
		AdvanceTileAnimation,
		RevertTileAnimation,		// TODO

		CreateControllablePlayer,
		CreateRemoteActor,
		CreateMirroredActor,
		DestroyRemoteActor,
		UpdateAllActors,

		PlayerMoveInstantly,
		PlayerAckWarped,			// TODO
		PlayerActivateForce,		// TODO
		PlayerAddHealth,			// TODO
		PlayerChangeWeapon,
		PlayerRefreshAmmo,
		PlayerRefreshWeaponUpgrades,
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