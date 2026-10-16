#pragma once

#include "../Main.h"
#include "GameDifficulty.h"
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

	/**
		@brief Options of loading a level

		Decides which of the events of a level are added to its @ref Events::EventMap --- each event carries the
		difficulties it's present in, and some are present only in multiplayer.
	*/
	struct LevelLoadOptions
	{
		/** @brief Difficulty of the session */
		GameDifficulty Difficulty = GameDifficulty::Default;
		/** @brief Whether the session is a multiplayer one, which also adds the multiplayer-only events */
		bool Multiplayer = false;
	};

	/**
		@brief Contains all components to fully initialize a level instance
		
		Produced when a level file is loaded and consumed by the level handler during setup. Bundles the parsed tile
		map and event map together with metadata such as display name, next/secret level names, music, ambient color,
		weather, water level and level texts.
	*/
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
		/** @brief Whether the level editor can open the level, see @ref LevelFlags::NotEditable */
		bool Editable = true;
	};
}