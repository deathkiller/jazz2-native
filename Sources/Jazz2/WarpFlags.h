#pragma once

#include "../Main.h"

namespace Jazz2
{
	/** Warp flags, supports a bitwise combination of its member values. */
	enum class WarpFlags
	{
		Default = 0,

		Fast = 0x01,
		Freeze = 0x02,
		SkipWarpIn = 0x04,

		IncrementLaps = 0x10
	};

	DEATH_ENUM_FLAGS(WarpFlags);
}