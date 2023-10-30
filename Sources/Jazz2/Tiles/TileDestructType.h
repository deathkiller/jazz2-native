#pragma once

#include "../../Common.h"

namespace Jazz2::Tiles
{
	enum class TileDestructType
	{
		None = 0x00,

		Weapon = 0x01,
		Speed = 0x02,
		Collapse = 0x04,
		Special = 0x08,
		Trigger = 0x10,

		IgnoreSolidTiles = 0x20
	};

	DEFINE_ENUM_OPERATORS(TileDestructType);
}