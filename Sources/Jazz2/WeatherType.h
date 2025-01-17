#pragma once

#include "../Main.h"

namespace Jazz2
{
	/** @brief Weather type, supports a bitwise combination of its member values */
	enum class WeatherType : std::uint8_t
	{
		None,						/**< None */

		Snow,						/**< Snow */
		Flowers,					/**< Flowers */
		Rain,						/**< Rain */
		Leaf,						/**< Leaf */

		OutdoorsOnly = 0x80			/**< Outdoors only, should be used in combination with other options */
	};

	DEATH_ENUM_FLAGS(WeatherType);
}