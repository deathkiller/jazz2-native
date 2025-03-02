#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MpGameMode.h"
#include "../LevelInitialization.h"

namespace Jazz2::Multiplayer
{
	/**
		@brief Server initialization parameters

		@experimental
	*/
	struct ServerInitialization
	{
		/** @brief Level initialization parameters */
		LevelInitialization InitialLevel;
		/** @brief Multiplayer game mode */
		MpGameMode GameMode;
		/** @brief Server name */
		String ServerName;
		/** @brief Server port */
		std::uint16_t ServerPort;
	};
}

#endif