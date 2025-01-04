#pragma once

#include "../Main.h"

namespace Jazz2
{
	/** @brief Exit type, supports a bitwise combination of its member values */
	enum class ExitType : std::uint8_t
	{
		None,

		Normal,
		Warp,
		Bonus,
		Special,
		Boss,

		TypeMask = 0x0f,
		Frozen = 0x40,
		FastTransition = 0x80
	};

	DEATH_ENUM_FLAGS(ExitType);
}