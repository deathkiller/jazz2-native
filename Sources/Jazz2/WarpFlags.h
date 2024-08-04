#pragma once

#include "../Common.h"

namespace Jazz2
{
	enum class WarpFlags
	{
		Default = 0,

		Fast = 0x01,
		Freeze = 0x02,
		SkipWarpIn = 0x04,

		IncrementLaps = 0x10
	};

	DEFINE_ENUM_OPERATORS(WarpFlags);
}