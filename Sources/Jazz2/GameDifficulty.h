#pragma once

#include "../Main.h"

namespace Jazz2
{
	enum class GameDifficulty : std::uint8_t
	{
		Default,

		Easy,
		Normal,
		Hard,

		Multiplayer
	};
}