#pragma once

#include "../Main.h"

namespace Jazz2
{
	enum class Direction
	{
		None = 0,

		Up = 0x01,
		Down = 0x02,
		Left = 0x04,
		Right = 0x08,

		Any = Up | Down | Left | Right
	};

	DEFINE_ENUM_OPERATORS(Direction);
}