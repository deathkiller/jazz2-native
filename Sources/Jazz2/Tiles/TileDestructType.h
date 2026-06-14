#pragma once

#include "../../Main.h"

namespace Jazz2::Tiles
{
	/** @brief Flags that specify type of collision with tile map, supports a bitwise combination of its member values */
	enum class TileDestructType
	{
		None = 0x00,			/**< No collision */

		Weapon = 0x01,			/**< Destroyed by a weapon */
		Speed = 0x02,			/**< Destroyed by movement speed */
		Collapse = 0x04,		/**< Collapsible tile */
		Special = 0x08,			/**< Destroyed by a special move */
		Trigger = 0x10,			/**< Trigger tile */

		IgnoreSolidTiles = 0x20,	/**< Solid tiles are ignored during collision */
		VerticalMove = 0x40,		/**< Movement is vertical */
	};

	DEATH_ENUM_FLAGS(TileDestructType);
}