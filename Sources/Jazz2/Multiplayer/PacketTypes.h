#pragma once

#if defined(WITH_MULTIPLAYER)

#include "../../Common.h"

namespace Jazz2::Multiplayer
{
	enum class ClientPacketType
	{
		Null,
		Ping,

		Auth,
		LevelReady,

		PlayerUpdate,
		PlayerRefreshAnimation,
		PlayerFireWeapon,
		PlayerDied,

		CreateRemotableActor,
		UpdateRemotableActor,
		DestroyRemotableActor
	};

	enum class ServerPacketType
	{
		Null,
		Pong,

		LoadLevel,
		PlaySfx,
		ShowMessage,
		OverrideLevelText,
		SetTrigger,
		AdvanceTileAnimation,
		RevertTileAnimation,

		CreateControllablePlayer,
		CreateRemoteActor,
		RefreshActorAnimation,
		DestroyRemoteActor,
		UpdateAllActors,

		PlayerMoveInstantly,
		PlayerActivateForce,
		PlayerAddHealth,
		PlayerRefreshAmmo,
		PlayerRefreshWeaponUpgrades,
		PlayerSetControllable,
		PlayerSetDizzyTime,
		PlayerSetInvulnerability,
		PlayerSetLaps,
		PlayerSetModifier,
		PlayerSetStats,
		PlayerTakeDamage,
		PlayerWarpToPosition
	};
}

#endif