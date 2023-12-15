#pragma once

#if defined(WITH_MULTIPLAYER)

namespace Jazz2::Multiplayer
{
	enum class MultiplayerGameMode
	{
		Unknown = 0,

		Battle,
		TeamBattle,
		CaptureTheFlag,
		Race,
		TreasureHunt,
		Cooperation
	};
}

#endif