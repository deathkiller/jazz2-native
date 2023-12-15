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
		PlayerKeyPress,
		PlayerRefreshAnimation,
		PlayerDied,

		CreateRemotableActor,
		UpdateRemotableActor,
		DestroyRemotableActor
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
		ShowMessage,
		OverrideLevelText,
		SyncTileMap,
		SetTrigger,
		AdvanceTileAnimation,
		RevertTileAnimation,

		CreateControllablePlayer,
		CreateRemoteActor,
		RefreshActorAnimation,
		DestroyRemoteActor,
		UpdateAllActors,

		PlayerMoveInstantly,
		PlayerAckWarped,
		PlayerActivateForce,
		PlayerAddHealth,
		PlayerChangeWeapon,
		PlayerRefreshAmmo,
		PlayerRefreshWeaponUpgrades,
		PlayerSetControllable,
		PlayerSetDizzyTime,
		PlayerSetInvulnerability,
		PlayerSetLaps,
		PlayerSetModifier,
		PlayerSetStats,
		PlayerTakeDamage,
		PlayerActivateSpring,
		PlayerWarpToPosition
	};
}

#endif