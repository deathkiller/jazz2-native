#pragma once

#include "../Main.h"

namespace Jazz2
{
	/**
		@brief Weather type
		
		Selects the falling-particle weather effect rendered over a level --- snow, flowers, rain or leaves --- or
		@ref WeatherType::None for no effect. The @ref WeatherType::OutdoorsOnly flag can be combined with a type so
		the effect is only shown in outdoor areas, @ref WeatherType::Inactive keeps the type for a script to start.
		Stored in @ref LevelDescriptor and set on the level handler. Supports a bitwise combination of its member values.
	*/
	enum class WeatherType : std::uint8_t
	{
		None,						/**< None */

		Snow,						/**< Snow */
		Flowers,					/**< Flowers */
		Rain,						/**< Rain */
		Leaf,						/**< Leaf */

		Inactive = 0x40,			/**< Nothing falls, the type and @ref WeatherType::OutdoorsOnly are kept for a script to start it */
		OutdoorsOnly = 0x80			/**< Outdoors only, should be used in combination with other options */
	};

	DEATH_ENUM_FLAGS(WeatherType);

	/** @brief Returns the type of weather without its flags */
	constexpr WeatherType GetWeatherKind(WeatherType type)
	{
		return WeatherType(std::uint8_t(type) & ~(std::uint8_t(WeatherType::Inactive) | std::uint8_t(WeatherType::OutdoorsOnly)));
	}

	/** @brief Returns whether the weather falls */
	constexpr bool IsWeatherActive(WeatherType type)
	{
		return (GetWeatherKind(type) != WeatherType::None && (std::uint8_t(type) & std::uint8_t(WeatherType::Inactive)) == 0);
	}
}