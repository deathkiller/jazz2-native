#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

namespace Jazz2::Multiplayer
{
	/** @brief Multiplayer game mode */
	enum class MultiplayerGameMode
	{
		Unknown = 0,

		Battle,
		TeamBattle,
		CaptureTheFlag,
		Race,
		TeamRace,
		TreasureHunt,
		Cooperation
	};
}

#endif