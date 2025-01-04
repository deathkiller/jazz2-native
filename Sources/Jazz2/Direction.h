#pragma once

#include "../Main.h"

namespace Jazz2
{
	/** @brief Direction, supports a bitwise combination of its member values */
	enum class Direction
	{
		None = 0,

		Up = 0x01,
		Down = 0x02,
		Left = 0x04,
		Right = 0x08,

		Any = Up | Down | Left | Right
	};

	DEATH_ENUM_FLAGS(Direction);
}