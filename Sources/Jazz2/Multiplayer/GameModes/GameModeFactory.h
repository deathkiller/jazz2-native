#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "IGameMode.h"
#include "../MpGameMode.h"

#include <memory>

namespace Jazz2::Multiplayer
{
	/**
		@brief Creates the @ref IGameMode implementation for the specified game mode

		Returns the rules object a level handler should host for @p mode. Every game mode has an
		implementation; modes still in the staged migration own only their HUD and report
		@ref IGameMode::OwnsRoundLogic as `false`. Returns `nullptr` only for @ref MpGameMode::Unknown.
	*/
	std::unique_ptr<IGameMode> CreateGameMode(MpGameMode mode);
}

#endif
