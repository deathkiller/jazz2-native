#pragma once

#include "../../Common.h"
#include "TileDestructType.h"
#include "../WeaponType.h"

namespace Jazz2::Tiles
{
	struct TileCollisionParams
	{
		TileDestructType DestructType;
		bool Downwards;
		WeaponType UsedWeaponType;
		std::int32_t WeaponStrength;
		float Speed;
		/*out*/ std::int32_t TilesDestroyed;
	};
}