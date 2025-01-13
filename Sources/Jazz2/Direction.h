#pragma once

#include "../Main.h"

namespace Jazz2
{
	/** @brief Direction, supports a bitwise combination of its member values */
	enum class Direction
	{
		None = 0,							/**< None */

		Up = 0x01,							/**< Up */
		Down = 0x02,						/**< Down */
		Left = 0x04,						/**< Left */
		Right = 0x08,						/**< Right */

		Any = Up | Down | Left | Right		/**< Any */
	};

	DEATH_ENUM_FLAGS(Direction);
}