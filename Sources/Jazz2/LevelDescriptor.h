#pragma once

#include "../Main.h"
#include "WeatherType.h"

#include "../nCine/Primitives/Vector4.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2
{
	namespace Events
	{
		class EventMap;
	}

	namespace Tiles
	{
		class TileMap;
	}

	/** @brief Contains all components to fully initialize a level instance */
	struct LevelDescriptor
	{
		/** @brief Full path */
		String FullPath;
		/** @brief Display name */
		String DisplayName;
		/** @brief Next level name */
		String NextLevel;
		/** @brief Secret level name */
		String SecretLevel;
		/** @brief Bonus level name */
		String BonusLevel;	// TODO: Unused
		/** @brief Tile map */
		std::unique_ptr<Tiles::TileMap> TileMap;
		/** @brief Event map */
		std::unique_ptr<Events::EventMap> EventMap;
		/** @brief Music file path */
		String MusicPath;
		/** @brief Ambient color */
		Vector4f AmbientColor;
		/** @brief Weather type */
		WeatherType Weather;
		/** @brief Weather intensity */
		std::uint8_t WeatherIntensity;
		/** @brief Water level */
		std::uint16_t WaterLevel;
		/** @brief Level texts */
		SmallVector<String, 0> LevelTexts;
	};
}