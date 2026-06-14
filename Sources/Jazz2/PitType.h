#pragma once

namespace Jazz2
{
	/**
		@brief Level pit type
		
		Determines what happens to a player who reaches the bottom of the level. The player can fall forever out of
		the level boundaries, die instantly, or be kept inside the level by standing on an invisible platform at the
		bottom edge. Derived from the level's pit-related flags and used to clamp or kill players below the level.
	*/
	enum class PitType
	{
		FallForever,			/**< Player should fall forever out of the level boundaries */
		InstantDeathPit,		/**< Player should die instantly out of the level boundaries */
		StandOnPlatform,		/**< Player should stand on platform, not leaving the level boundaries */
	};
}