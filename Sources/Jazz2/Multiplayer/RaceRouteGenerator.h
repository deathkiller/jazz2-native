#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MpLevelHandler.h"

namespace Jazz2::Multiplayer
{
	/** @brief Shape of the route traced by @ref GenerateRaceRouteFromGeometry() */
	enum class TrackRouteType
	{
		Lap,			/**< Race track: from the spawn around the loop and back to the "Set Lap" warp (or a level exit if there is none) */
		LevelExit		/**< Straight from the spawn to the level exit, e.g. through a story level in Cooperation */
	};

	/**
		@brief Auto-places minimap track checkpoints for levels without authored waypoints

		Heuristic route tracer for levels that don't carry JJ2+ waypoint Text events. It traces the actual walkable
		route the player would take and samples checkpoints evenly along it, so the minimap (and in race modes also
		the progress-based position ranking) reflects the real level geometry. @ref TrackRouteType::Lap goes from
		the spawn to the farthest reachable point and back to the "Set Lap" warp, @ref TrackRouteType::LevelExit
		straight from the spawn to the level exit.

		The player's movement is traced against the tile masks at sub-tile precision, and jumps, springs, poles and
		tube rides are simulated with the original game's physics, including the moves only some characters have
		(a cooperation player can pick any). The player swims under @p waterLevel (in pixels, @cpp FLT_MAX @ce if
		the level has no water), flies with an airboard or a copter, and uses trigger tiles in either state. A
		@ref TrackRouteType::LevelExit route leads to the boss in a level without an exit, and out to the far end
		in a level with neither.

		On success, fills @p orderedCheckpoints, @p outBoundsMin / @p outBoundsMax (minimap extent in tiles)
		and sets @p outCheckpointsOrdered to `true`. On failure, the outputs are left unchanged (except an
		emptied @p orderedCheckpoints when a traced route turns out to be degenerate).
	*/
	void GenerateRaceRouteFromGeometry(Tiles::TileMap* tileMap, Events::EventMap* eventMap, TrackRouteType routeType, float waterLevel,
		const SmallVector<MultiplayerSpawnPoint, 0>& spawnPoints, const SmallVector<Vector2i, 0>& startMarkers,
		SmallVector<RaceCheckpoint, 0>& orderedCheckpoints, Vector2i& outBoundsMin, Vector2i& outBoundsMax,
		bool& outCheckpointsOrdered);
}

#endif
