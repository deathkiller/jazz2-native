#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MpGameMode.h"

#include "../../nCine/Primitives/Colorf.h"

#include <Containers/StringView.h>

namespace Jazz2::Multiplayer
{
	/** @brief Maximum number of teams supported */
	static constexpr std::uint8_t MaxTeamCount = 4;
	/** @brief Sentinel team value meaning "no preference, let the server decide" */
	static constexpr std::uint8_t NoPreferredTeam = 0xFF;

	/** @brief Returns `true` if the specified game mode splits players into teams */
	inline bool IsTeamGameMode(MpGameMode mode)
	{
		switch (mode) {
			case MpGameMode::TeamBattle:
			case MpGameMode::TeamRace:
			case MpGameMode::TeamTreasureHunt:
			case MpGameMode::CaptureTheFlag:
				return true;
			default:
				return false;
		}
	}

	/** @brief Returns the display color of the specified team (Blue, Red, Green, Yellow) */
	inline nCine::Colorf GetTeamColor(std::uint8_t team)
	{
		switch (team) {
			case 0: return nCine::Colorf(0.3f, 0.48f, 0.74f, 1.0f);		// Blue
			case 1: return nCine::Colorf(0.68f, 0.36f, 0.36f, 1.0f);	// Red
			case 2: return nCine::Colorf(0.34f, 0.48f, 0.32f, 1.0f);	// Green
			case 3: return nCine::Colorf(0.62f, 0.44f, 0.34f, 1.0f);	// Yellow
			default: return nCine::Colorf(0.7f, 0.7f, 0.7f, 1.0f);		// Neutral
		}
	}

	/** @brief Returns the display name of the specified team */
	inline Death::Containers::StringView GetTeamName(std::uint8_t team)
	{
		switch (team) {
			case 0: return "Blue";
			case 1: return "Red";
			case 2: return "Green";
			case 3: return "Yellow";
			default: return "Neutral";
		}
	}

	/**
		@brief Recolors a packed fur color so the primary character color section matches the team color

		Forces the low byte (the first of the four packed fur sections, i.e. the primary fur color) of @p furColor to
		the team's signature sprite-palette gradient start, keeping the player's own choice for the remaining sections.
		The gradient bytes match the in-game character color options (@ref Jazz2::ContentResolver::FurHueShiftFlag =
		`0x80` marks a hue-shifted variant): Red = `0x18`, Blue = `0x20`, Yellow = `0x28`, Green = `0x58 | 0x80`
		(the last hue-shifted variant). Unknown/neutral teams are left unchanged.
	*/
	inline std::uint32_t ApplyTeamFurColor(std::uint32_t furColor, std::uint8_t team)
	{
		std::uint8_t gradient;
		switch (team) {
			case 0: gradient = 0x20; break;			// Blue
			case 1: gradient = 0x18; break;			// Red
			case 2: gradient = 0x58 | 0x80; break;	// Green (hue-shifted variant)
			case 3: gradient = 0x28; break;			// Yellow
			default: return furColor;				// Neutral / no team - leave the player's own color
		}
		return (furColor & 0xFFFFFF00u) | (std::uint32_t)gradient;
	}
}

#endif
