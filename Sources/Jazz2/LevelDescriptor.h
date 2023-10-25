#pragma once

#include "../Common.h"
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

	struct LevelDescriptor
	{
		String FullPath;
		String DisplayName;
		String NextLevel;
		String SecretLevel;
		String BonusLevel;	// TODO: Unused
		std::unique_ptr<Tiles::TileMap> TileMap;
		std::unique_ptr<Events::EventMap> EventMap;
		String MusicPath;
		Vector4f AmbientColor;
		WeatherType Weather;
		std::uint8_t WeatherIntensity;
		std::uint16_t WaterLevel;
		SmallVector<String, 0> LevelTexts;
	};
}