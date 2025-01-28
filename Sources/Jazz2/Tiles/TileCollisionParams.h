#pragma once

#include "../../Main.h"
#include "TileDestructType.h"
#include "../WeaponType.h"

namespace Jazz2::Tiles
{
	/** @brief Describes how the object interacts and collides with the environment */
	struct TileCollisionParams
	{
		/** @brief Destruction type */
		TileDestructType DestructType;
		/** @brief Whether movement direction is downwards */
		bool Downwards;
		/** @brief Used weapon type */
		WeaponType UsedWeaponType;
		/** @brief Remaining weapon strength */
		std::int32_t WeaponStrength;
		/** @brief Movement speed */
		float Speed;
		/** @brief Number of destroyed tiles */
		/*out*/ std::int32_t TilesDestroyed;
	};
}