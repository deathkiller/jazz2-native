#pragma once

#include "../Main.h"

namespace Jazz2
{
	/**
		@brief Warp flags
		
		Options controlling how a player warp is performed, as single-bit flags over the @ref WarpFlags::Default
		behaviour: @ref WarpFlags::Fast skips the warp transitions, @ref WarpFlags::Freeze freezes the player when the
		transition ends, @ref WarpFlags::SkipWarpIn makes the player disappear instantly without the warp-in
		animation, and @ref WarpFlags::IncrementLaps counts a lap in race levels. Passed when a player is warped.
		Supports a bitwise combination of its member values.
	*/
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