#pragma once

#include "../Main.h"

namespace Jazz2
{
	/** @brief Level flags, supports a bitwise combination of its member values */
	enum class LevelFlags : std::uint16_t
	{
		None = 0,
		HasPit = 0x01,
		HasPitInstantDeath = 0x02,
		UseLevelPalette = 0x04,
		IsHidden = 0x08,
		IsMultiplayerLevel = 0x10,
		HasLaps = 0x20,
		HasCaptureTheFlag = 0x40,
		HasVerticalSplitscreen = 0x80,
		HasMultiplayerSpawnPoints = 0x100
	};

	DEFINE_ENUM_OPERATORS(LevelFlags);
}