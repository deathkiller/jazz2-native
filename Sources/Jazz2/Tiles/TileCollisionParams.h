#pragma once

#include "../../Main.h"
#include "TileDestructType.h"
#include "../WeaponType.h"

namespace Jazz2::Tiles
{
	/** @brief Describes how the object interacts and collides with the environment */
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