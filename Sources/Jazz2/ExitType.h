#pragma once

#include "../Common.h"

namespace Jazz2
{
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

	DEFINE_ENUM_OPERATORS(ExitType);
}