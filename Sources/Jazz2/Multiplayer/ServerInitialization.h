#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MpGameMode.h"
#include "../LevelInitialization.h"

namespace Jazz2::Multiplayer
{
	/** @brief Server initialization */
	struct ServerInitialization
	{
		LevelInitialization InitialLevel;
		MpGameMode GameMode;
		String ServerName;
		std::uint16_t ServerPort;

		//ServerInitialization();

		//LevelInitialization(StringView episode, StringView level, GameDifficulty difficulty, bool isReforged);
		//LevelInitialization(StringView episode, StringView level, GameDifficulty difficulty, bool isReforged, bool cheatsUsed, PlayerType playerType);
		//LevelInitialization(StringView episode, StringView level, GameDifficulty difficulty, bool isReforged, bool cheatsUsed, ArrayView<const PlayerType> playerTypes);

		//LevelInitialization(const LevelInitialization& copy);
		//LevelInitialization(LevelInitialization&& move);
	};
}

#endif