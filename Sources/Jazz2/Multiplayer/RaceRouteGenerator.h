#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MpLevelHandler.h"

namespace Jazz2::Multiplayer
{
	/**
		@brief Auto-places race checkpoints for levels without authored waypoints

		Heuristic route tracer for original race levels that don't carry JJ2+ waypoint Text events. It traces
		the actual walkable route the player would take (spawn, farthest reachable point, back to the "Set Lap"
		warp) and samples checkpoints evenly along it, so the minimap and progress-based position ranking
		reflect the real level geometry.

		On success, fills @p orderedCheckpoints, @p outBoundsMin / @p outBoundsMax (minimap extent in tiles)
		and sets @p outCheckpointsOrdered to `true`. On failure, the outputs are left unchanged (except an
		emptied @p orderedCheckpoints when a traced route turns out to be degenerate).
	*/
	void GenerateRaceRouteFromGeometry(Tiles::TileMap* tileMap, Events::EventMap* eventMap,
		const SmallVector<MultiplayerSpawnPoint, 0>& spawnPoints, const SmallVector<Vector2i, 0>& startMarkers,
		SmallVector<RaceCheckpoint, 0>& orderedCheckpoints, Vector2i& outBoundsMin, Vector2i& outBoundsMax,
		bool& outCheckpointsOrdered);
}

#endif
