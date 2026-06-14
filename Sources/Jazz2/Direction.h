#pragma once

#include "../Main.h"

namespace Jazz2
{
	/**
		@brief Direction
		
		Identifies one or more of the four cardinal directions as single-bit flags, with @ref Direction::Any covering
		all of them and @ref Direction::None covering none. Used throughout the engine to describe directional state
		such as which sides of an object are relevant. Supports a bitwise combination of its member values.
	*/
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