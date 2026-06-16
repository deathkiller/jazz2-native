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
			case 2: return nCine::Colorf(0.36f, 0.80f, 0.40f, 1.0f);	// Green
			case 3: return nCine::Colorf(0.8f, 0.76f, 0.32f, 1.0f);		// Yellow
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
}

#endif
