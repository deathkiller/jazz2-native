#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

namespace Jazz2::Multiplayer
{
	/**
		@brief Multiplayer game mode

		Selects the win condition and team rules of a multiplayer round, from free-for-all and team variants
		of Battle, Race and Treasure Hunt to Capture The Flag, Jailbreak, Domination and Cooperation.
		The value is sent over the network, so new modes are only appended.
	*/
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
		Cooperation,		/**< Cooperation */

		RoastTag,						/**< Roast Tag */
		LastRabbitStanding,				/**< Last Rabbit Standing */
		ExtendedLastRabbitStanding,		/**< Extended Last Rabbit Standing */
		Pestilence,						/**< Pestilence */
		TeamLastRabbitStanding,			/**< Team Last Rabbit Standing */
		Jailbreak,						/**< Jailbreak */
		DeathCaptureTheFlag,			/**< Death Capture The Flag */
		FlagRun,						/**< Flag Run */
		Domination,						/**< Domination */
		HeadHunters						/**< Head Hunters */
	};
}

#endif
