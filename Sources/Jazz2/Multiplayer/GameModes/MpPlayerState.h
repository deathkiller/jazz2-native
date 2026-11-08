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

		/** @brief Order in which the player completed all required laps (`0` while still racing) */
		std::uint32_t RaceFinishOrder;

		/** @brief Timestamp when the last lap started */
		TimeStamp LapStarted;
		/** @brief Elapsed frames when the player lost all lives (`FLT_MAX` while alive) */
		float DeathElapsedFrames;
		/** @brief Elapsed frames of all completed laps */
		float LapsElapsedFrames;
		/** @brief Elapsed frames when the player completed the last required lap (valid if @ref RaceFinishOrder is not zero) */
		float RaceFinishFrames;
		/** @brief Distance travelled along the race track in the current lap, in pixels */
		float RaceProgress;

		/** @brief Score defined by the game mode, e.g. tag points or banked heads (synced to all peers) */
		std::uint32_t ModePoints;
		/** @brief Amount of something the player carries in the game mode, e.g. heads (synced to all peers) */
		std::uint32_t ModeCarried;
		/** @brief Lives left in modes that count them (synced to all peers) */
		std::int32_t ModeLives;
		/** @brief Countdown in frames used by the game mode, e.g. until a zombie returns to life */
		float ModeTimer;
		/** @brief Elapsed frames when the player last spawned or joined */
		float SpawnedFrames;
		/** @brief Role of the player defined by the game mode, see @ref GameModes::PlayerModeFlags (synced to all peers) */
		std::uint8_t ModeFlags;
		/** @brief Flags the game mode carries over to the next round (not reset), e.g. the first zombie of the next round */
		std::uint8_t ModeCarryOverFlags;

		MpPlayerState()
			: Team(0), PreferredTeam(0xFF), TeamLocked(false), TeamSwitchCooldown(0.0f),
				PointsInRound(0), PositionInRound(0), Deaths(0), Kills(0), Laps(0), TreasureCollected(0),
				RaceFinishOrder(0), DeathElapsedFrames(FLT_MAX), LapsElapsedFrames(0.0f), RaceFinishFrames(0.0f),
				RaceProgress(0.0f), ModePoints(0), ModeCarried(0), ModeLives(0), ModeTimer(0.0f), SpawnedFrames(0.0f),
				ModeFlags(0), ModeCarryOverFlags(0)
		{
		}
	};
}

#endif
