#pragma once

#include "../Main.h"

namespace Jazz2
{
	/** Warp flags, supports a bitwise combination of its member values */
	enum class WarpFlags
	{
		Default = 0,				/**< Standard warp */

		Fast = 0x01,				/**< Fast warp, without transitions */
		Freeze = 0x02,				/**< Freezes the player at the end of transition */
		SkipWarpIn = 0x04,			/**< Skips warp-in transition, the player disappears instantly */

		IncrementLaps = 0x10		/**< Number of laps will be incremented (in race levels) */
	};

	DEATH_ENUM_FLAGS(WarpFlags);
}