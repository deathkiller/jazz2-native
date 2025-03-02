#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

namespace Jazz2::Multiplayer
{
	/** @brief Multiplayer game mode */
	enum class MpGameMode
	{
		Unknown = 0,		/**< Unspecified */

		Battle,				/**< Battle */
		TeamBattle,			/**< Team battle */
		CaptureTheFlag,		/**< Capture the flag */
		Race,				/**< Race */
		TeamRace,			/**< Team race */
		TreasureHunt,		/**< Treasure hunt */
		Cooperation			/**< Cooperation */
	};
}

#endif