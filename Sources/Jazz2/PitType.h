#pragma once

namespace Jazz2
{
	/** @brief Level pit type */
	enum class PitType
	{
		FallForever,			/**< Player should fall forever out of the level boundaries */
		InstantDeathPit,		/**< Player should die instantly out of the level boundaries */
		StandOnPlatform,		/**< Player should stand on platform, not leaving the level boundaries */
	};
}