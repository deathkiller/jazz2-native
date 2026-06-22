#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../nCine/Base/TimeStamp.h"

#include <cfloat>
#include <cstdint>

using namespace nCine;

namespace Jazz2::Multiplayer
{
	/**
		@brief Network-agnostic per-player game-mode state for a single round

		Holds the round statistics and team assignment that the game modes read and write, independent of
		any networking concept. In online sessions @ref PeerDescriptor inherits this; in local splitscreen
		it is owned directly by the local handler. Keeping per-round state here lets the game modes read and
		write it through @ref IGameModeContext without knowing about peers or networking.
	*/
	struct MpPlayerState
	{
		/** @brief Team ID */
		std::uint8_t Team;
		/** @brief Preferred team requested by the player (`0xFF` = no preference, let the host decide) */
		std::uint8_t PreferredTeam;
		/** @brief Whether the team was forced (admin/rebalance); client requests are rejected while set */
		bool TeamLocked;
		/** @brief Remaining frames before the player may switch teams again */
		float TeamSwitchCooldown;

		/** @brief Game-mode specific points held in the current round */
		std::uint32_t PointsInRound;
		/** @brief Position in the current round */
		std::uint32_t PositionInRound;
		/** @brief Deaths in the current round */
		std::uint32_t Deaths;
		/** @brief Kills in the current round */
		std::uint32_t Kills;
		/** @brief Completed laps in the current round */
		std::uint32_t Laps;
		/** @brief Treasure collected in the current round */
		std::uint32_t TreasureCollected;

		/** @brief Timestamp when the last lap started */
		TimeStamp LapStarted;
		/** @brief Elapsed frames when the player lost all lives (`FLT_MAX` while alive) */
		float DeathElapsedFrames;
		/** @brief Elapsed frames of all completed laps */
		float LapsElapsedFrames;

		MpPlayerState()
			: Team(0), PreferredTeam(0xFF), TeamLocked(false), TeamSwitchCooldown(0.0f),
				PointsInRound(0), PositionInRound(0), Deaths(0), Kills(0), Laps(0), TreasureCollected(0),
				DeathElapsedFrames(FLT_MAX), LapsElapsedFrames(0.0f)
		{
		}
	};
}

#endif
