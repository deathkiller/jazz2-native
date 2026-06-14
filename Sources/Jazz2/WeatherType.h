#pragma once

#include "../Main.h"

namespace Jazz2
{
	/**
		@brief Weather type
		
		Selects the falling-particle weather effect rendered over a level --- snow, flowers, rain or leaves --- or
		@ref WeatherType::None for no effect. The @ref WeatherType::OutdoorsOnly flag can be combined with a type so
		the effect is only shown in outdoor areas. Stored in @ref LevelDescriptor and set on the level handler.
		Supports a bitwise combination of its member values.
	*/
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