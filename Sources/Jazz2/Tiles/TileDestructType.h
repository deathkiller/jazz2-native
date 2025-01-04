#pragma once

#include "../../Main.h"

namespace Jazz2::Tiles
{
	/** @brief Flags that specify type of collision with tile map, supports a bitwise combination of its member values */
	enum class TileDestructType
	{
		None = 0x00,

		Weapon = 0x01,
		Speed = 0x02,
		Collapse = 0x04,
		Special = 0x08,
		Trigger = 0x10,

		IgnoreSolidTiles = 0x20,
		VerticalMove = 0x40,
	};

	DEATH_ENUM_FLAGS(TileDestructType);
}