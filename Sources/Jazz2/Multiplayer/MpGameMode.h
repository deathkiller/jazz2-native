#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

namespace Jazz2::Multiplayer
{
	/** @brief Multiplayer game mode */
	enum class MpGameMode
	{
		Unknown = 0,		/**< Unspecified */

		Battle,				/**< Battle */
		TeamBattle,			/**< Team Battle */
		Race,				/**< Race */
		TeamRace,			/**< Team Race */
		TreasureHunt,		/**< Treasure Hunt */
		TeamTreasureHunt,	/**< Team Treasure Hunt */
		CaptureTheFlag,		/**< Capture The Flag */
		Cooperation			/**< Cooperation */
	};
}

#endif