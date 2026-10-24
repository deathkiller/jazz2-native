#pragma once

#include "../Main.h"
#include "GameDifficulty.h"
#include "WeatherType.h"
#include "Formats/LevelFile.h"

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
		@brief Texts of a level translated to one language

		A level may carry its texts in more languages (see @ref Formats::LevelTextsChunk), the game shows those of the
		language it's in, and the texts of the level itself where there's no translation.
	*/
	struct LocalizedLevelTexts
	{
		/** @brief Language, as the game names it in its settings, such as `cs` or `pt-br` */
		String Language;
		/** @brief Texts by their number, an empty one isn't translated */
		SmallVector<String, 0> Texts;
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
		/** @brief Level texts translated to other languages, empty if the level has none */
		SmallVector<LocalizedLevelTexts, 0> LocalizedTexts;
		/** @brief Level texts as the original game had them, for the script of the level, empty if it has none (see @ref Formats::ScriptTextsChunk) */
		SmallVector<String, 0> ScriptTexts;
		/** @brief Custom weapons the level sets up for its script, empty if it has none (see @ref Formats::CustomWeaponsChunk) */
		SmallVector<std::uint8_t, 0> CustomWeapons;
		/** @brief Sprites the level recolors for its script, empty if it has none (see @ref Formats::SpriteRecolorsChunk) */
		SmallVector<std::uint8_t, 0> SpriteRecolors;
		/** @brief Tile images the level replaces, which scripts read instead of those of the tile sets */
		SmallVector<Formats::TileDiffuseOverride, 0> TileDiffuseOverrides;
	};
}