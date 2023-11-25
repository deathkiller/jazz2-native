#pragma once

#include "../Common.h"

namespace Jazz2
{
	enum class WeatherType : std::uint8_t
	{
		None,

		Snow,
		Flowers,
		Rain,
		Leaf,

		OutdoorsOnly = 0x80
	};

	DEFINE_ENUM_OPERATORS(WeatherType);
}