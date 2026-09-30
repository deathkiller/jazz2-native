#include "RaceRouteGenerator.h"

#if defined(WITH_MULTIPLAYER)

#include "../Tiles/TileMap.h"
#include "../Events/EventMap.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <vector>

#include "../../nCine/Base/HashMap.h"

using namespace nCine;

namespace Jazz2::Multiplayer
{
	namespace
	{
		struct TileCoordHash {
#if defined(DEATH_TARGET_32BIT)
			using IntHash = xxHash32Func<std::int32_t>;
#else
			using IntHash = xxHash64Func<std::int32_t>;
#endif

			std::size_t operator()(const Vector2i& coord) const {
				return IntHash()(coord.X) ^ (IntHash()(coord.Y) << 1);
			}
		};
	}

	void GenerateRaceRouteFromGeometry(Tiles::TileMap* tileMap, Events::EventMap* eventMap, TrackRouteType routeType, float waterLevel,
		const SmallVector<MultiplayerSpawnPoint, 0>& spawnPoints, const SmallVector<Vector2i, 0>& startMarkers,
		SmallVector<RaceCheckpoint, 0>& orderedCheckpoints, Vector2i& outBoundsMin, Vector2i& outBoundsMax,
		bool& outCheckpointsOrdered)
	{
		// Heuristic auto-placement for original levels that don't carry JJ2+ waypoint Text events.
		// We trace the actual walkable route the player would take. On a race track, that is from a spawn point,
		// out to the farthest reachable point (the far side of the loop), and back to the "Set Lap" warp via the
		// opposite arm of the track. Through a story level, it's simply from the spawn to the level exit.
		// Checkpoints are then sampled evenly along that route, so the minimap reflects the real level geometry
		// and starts at the spawn / ends at the finish.
		Vector2i gridSize = tileMap->GetSize();
		const std::int32_t W = gridSize.X, H = gridSize.Y;
		if (W <= 0 || H <= 0) {
			return;
		}

		Vector2f spawnPos;
		if (!spawnPoints.empty()) {
			// Multiple spawn points are usually clustered around the start line; aggregate the ones near each
			// other (within ~20 tiles of the first) into a single start point
			const float clusterRange = 20.0f * Tiles::TileSet::DefaultTileSize;
			Vector2f base = spawnPoints[0].Pos;
			Vector2f sum(0.0f, 0.0f);
			std::int32_t clustered = 0;
			for (const auto& sp : spawnPoints) {
				if ((sp.Pos - base).Length() <= clusterRange) {
					sum += sp.Pos;
					clustered++;
				}
			}
			spawnPos = (clustered > 0 ? sum / (float)clustered : base);
			LOGI("Auto-placing minimap track: aggregated {} of {} spawn point(s)", clustered, (std::int32_t)spawnPoints.size());
		} else {
			// Some levels start only one of the characters
			spawnPos = eventMap->GetSpawnPosition(PlayerType::Jazz);
			if (spawnPos.X < 0.0f || spawnPos.Y < 0.0f) {
				spawnPos = eventMap->GetSpawnPosition(PlayerType::Spaz);
			}
			if (spawnPos.X < 0.0f || spawnPos.Y < 0.0f) {
				spawnPos = eventMap->GetSpawnPosition(PlayerType::Lori);
			}
		}
		if (spawnPos.X < 0.0f || spawnPos.Y < 0.0f) {
			LOGW("Cannot auto-place minimap track: no valid spawn position");
			return;
		}

		// Movement model: the player can stand on solid ground or special surfaces (bridges/platforms), walk, fall,
		// jump a limited height/gap (without passing through solid tiles), ascend through float-up/vine/pole areas
		// and launch high off springs - but cannot fly. This keeps the traced route on the real player path.
		const std::int32_t totalTiles = W * H;

		// A tile is passable if it's empty, destructible (player breaks it) or a one-way platform (passable from
		// below). Trigger-controlled tiles are treated as PASSABLE (their open state): a trigger crate toggles such
		// tiles - typically solid-by-default tiles that become invisible once triggered to open the way forward - so
		// assuming they're open lets the route pass through the path the crate reveals.
		auto isFree = [&](std::int32_t tx, std::int32_t ty) -> bool {
			if (tileMap->IsTileTrigger(tx, ty)) {
				return true;
			}
			return tileMap->IsTileEmpty(tx, ty) || tileMap->IsTileDestructible(tx, ty) || tileMap->IsTileOneWay(tx, ty);
		};
		auto isOneWay = [&](std::int32_t tx, std::int32_t ty) -> bool {
			return (tx >= 0 && ty >= 0 && tx < W && ty < H && tileMap->IsTileOneWay(tx, ty));
		};

		// Scan events for movement aids: springs (launch), lifts (ascend through float-up/vine/pole/hook) and
		// surfaces (bridges/moving platforms the player stands on over a gap)
		std::unique_ptr<std::uint8_t[]> springMap = std::make_unique<std::uint8_t[]>((std::size_t)totalTiles);
		std::unique_ptr<std::uint8_t[]> springBoost = std::make_unique<std::uint8_t[]>((std::size_t)totalTiles);
		std::unique_ptr<std::uint8_t[]> liftMap = std::make_unique<std::uint8_t[]>((std::size_t)totalTiles);
		std::unique_ptr<std::uint8_t[]> surfaceMap = std::make_unique<std::uint8_t[]>((std::size_t)totalTiles);
		std::unique_ptr<std::uint8_t[]> tubeMap = std::make_unique<std::uint8_t[]>((std::size_t)totalTiles);
		std::unique_ptr<std::int8_t[]> tubeSpeedX = std::make_unique<std::int8_t[]>((std::size_t)totalTiles);
		std::unique_ptr<std::int8_t[]> tubeSpeedY = std::make_unique<std::int8_t[]>((std::size_t)totalTiles);
		// Flight: 1 = a pickup that makes the player fly (airboard, copter, fly carrot), 2 = a Fly Off area ending it
		std::unique_ptr<std::uint8_t[]> flightMap = std::make_unique<std::uint8_t[]>((std::size_t)totalTiles);
		// Horizontal poles, which fling the player sideways
		std::unique_ptr<std::uint8_t[]> hPoleMap = std::make_unique<std::uint8_t[]>((std::size_t)totalTiles);
		// Warps that teleport the player to another section (so the route can continue from the destination). The
		// "Set Lap" warp (EventParams[2] != 0) is the lap finish and is handled separately, so it's excluded here.
		// Warp targets live in a separate EventMap list (not the event layout), so they're resolved via GetWarpTarget().
		SmallVector<Pair<Vector2i, std::uint32_t>, 0> warpOrigins;
		// Level-exit events (end-of-level area / sign): the finish of a route to the exit, and of a lap when there's
		// no "Set Lap" warp, mirroring how Race mode itself falls back to level exits for lap completion
		SmallVector<Vector2i, 0> exitMarkers;
		// Bosses: a boss level has no exit event, beating the boss ends it, so the boss is its finish instead
		SmallVector<Vector2i, 0> bossMarkers;
		// The highest the water surface gets, see inWater() below
		float highestWater = waterLevel;
		eventMap->ForEachEvent([&](Events::EventMap::EventTile& e, std::int32_t x, std::int32_t y) {
			if (x < 0 || y < 0 || x >= W || y >= H) {
				return true;
			}
			switch (e.Event) {
				case EventType::Spring: {
					// Orientation: 0 = vertical up, 2 = vertical down (ceiling), 4/5 = horizontal. Mark 1 for
					// "launch up", 2 for "launch sideways"; down springs don't help the player ascend, so ignore them.
					std::uint8_t orientation = e.EventParams[1];
					if (orientation == 4 || orientation == 5) {
						springMap[x + y * W] = 2;
					} else if (orientation == 0) {
						springMap[x + y * W] = 1;
						// Per-type lift height: red 9, green 15, blue 19 tiles (+1 for the landing)
						std::uint8_t type = e.EventParams[0];
						springBoost[x + y * W] = (std::uint8_t)((type == 0 ? 9 : type == 2 ? 19 : 15) + 1);
					}
					break;
				}
				case EventType::AreaFloatUp:
				case EventType::ModifierVine:
				case EventType::SwingingVine:
				case EventType::ModifierHook:
					// Gentle lift: float-up area / vine / hook - the player climbs and does a normal jump off
					liftMap[x + y * W] = 1;
					break;
				case EventType::PinballBumper:
				case EventType::PinballPaddle:
					// Pinball bumpers/paddles fling the player upward; model them as a moderate vertical spring
					springMap[x + y * W] = 1;
					springBoost[x + y * W] = 12;
					break;
				case EventType::Crate:
				case EventType::Barrel: {
					// A crate/barrel may contain a spring (CRATE_SPRING etc.) - the player breaks it and rides the
					// spring, so treat the tile as that spring. Params[0..1] = contained event type; the contained
					// event's own params follow at [3..], so [3] = spring type, [4] = orientation (as in Spring).
					std::uint16_t contained = (std::uint16_t)(e.EventParams[0] | (e.EventParams[1] << 8));
					if (contained == (std::uint16_t)EventType::Spring) {
						std::uint8_t orientation = e.EventParams[4];
						if (orientation == 4 || orientation == 5) {
							springMap[x + y * W] = 2;
						} else if (orientation == 0) {
							springMap[x + y * W] = 1;
							std::uint8_t type = e.EventParams[3];
							springBoost[x + y * W] = (std::uint8_t)((type == 0 ? 9 : type == 2 ? 19 : 15) + 1);
						}
					}
					break;
				}
				case EventType::ModifierVPole:
				case EventType::Pole:
					// Spinning pole: flings the player upward with amplified momentum (chains higher and higher),
					// often arranged in an offset zigzag - mark as a strong launcher with a long jump-off reach
					liftMap[x + y * W] = 2;
					break;
				case EventType::ModifierTube: {
					// Tubes transport the player through (often narrow/diagonal) passages regardless of geometry, in
					// the direction of their speed (XSpeed, YSpeed), kept as 1 + (sx + 1) + 3 * (sy + 1)
					std::int32_t sx = (std::int8_t)e.EventParams[0], sy = (std::int8_t)e.EventParams[1];
					tubeSpeedX[x + y * W] = (std::int8_t)sx;
					tubeSpeedY[x + y * W] = (std::int8_t)sy;
					sx = (sx > 0) - (sx < 0);
					sy = (sy > 0) - (sy < 0);
					tubeMap[x + y * W] = (std::uint8_t)(1 + (sx + 1) + 3 * (sy + 1));
					break;
				}
				case EventType::WarpOrigin:
					if (e.EventParams[2] == 0) {
						warpOrigins.push_back(pair(Vector2i(x, y), (std::uint32_t)e.EventParams[0]));
					}
					break;
				case EventType::AreaEndOfLevel:
				case EventType::SignEOL:
					// Skip secret exits (encoded as ExitType::Special) - they lead to a secret level, not the finish
					if (e.EventParams[0] != (std::uint8_t)ExitType::Special) {
						exitMarkers.push_back(Vector2i(x, y));
					}
					break;
				case EventType::AirboardGenerator:
				case EventType::Copter:
				case EventType::CarrotFly:
					flightMap[x + y * W] = 1;
					break;
				case EventType::ModifierHPole:
					hPoleMap[x + y * W] = 1;
					break;
				case EventType::PowerUpMorph:
					// A monitor that turns the player into a bird, which flies - levels are built around it the same
					// way as around a copter, although the engine doesn't morph into a bird yet
					if (e.EventParams[0] == 2) {
						flightMap[x + y * W] = 1;
					}
					break;
				case EventType::AreaFlyOff:
					flightMap[x + y * W] = 2;
					break;
				case EventType::ModifierSetWater:
					highestWater = std::min(highestWater, (float)(e.EventParams[0] | (e.EventParams[1] << 8)));
					break;
				case EventType::BossTweedle:
				case EventType::BossBilsy:
				case EventType::BossDevan:
				case EventType::BossQueen:
				case EventType::BossRobot:
				case EventType::BossUterus:
				case EventType::BossTurtleTough:
				case EventType::BossBubba:
				case EventType::BossDevanRemote:
				case EventType::BossBolly:
					bossMarkers.push_back(Vector2i(x, y));
					break;
				case EventType::Bridge: {
					// A bridge is a walkable surface extending to the right of the event tile (one extra tile past
					// each end for the actor's ~half-tile overhang). Mark only its EMPTY tiles: the span may overlap
					// solid anchor tiles, which ground the player on their own - flagging those as "bridge surface"
					// would wrongly suppress the downward jump on solid ground (see the jump loop below).
					std::int32_t widthTiles = ((e.EventParams[0] | (e.EventParams[1] << 8)) * 16) / Tiles::TileSet::DefaultTileSize;
					for (std::int32_t i = -1; i <= widthTiles + 1; i++) {
						if (x + i >= 0 && x + i < W && tileMap->IsTileEmpty(x + i, y)) {
							surfaceMap[(x + i) + y * W] = 1;
						}
					}
					break;
				}
				case EventType::MovingPlatform: {
					// A moving platform's anchor (the event tile) usually sits inside a wall; the platform sweeps a
					// circle of radius (length * 12px) around it. Mark the free tiles within that radius as a lift
					// zone (+ surface) so the tracer can board the platform anywhere along its path and ride up.
					std::int32_t radius = (e.EventParams[3] * 12 + Tiles::TileSet::DefaultTileSize - 1) / Tiles::TileSet::DefaultTileSize;
					if (radius < 1) {
						radius = 1;
					}
					for (std::int32_t dy = -radius; dy <= radius; dy++) {
						for (std::int32_t dx = -radius; dx <= radius; dx++) {
							if (dx * dx + dy * dy > radius * radius) {
								continue;
							}
							std::int32_t nx = x + dx, ny = y + dy;
							if (nx >= 0 && ny >= 0 && nx < W && ny < H && isFree(nx, ny)) {
								liftMap[nx + ny * W] = 1;
								surfaceMap[nx + ny * W] = 1;
							}
						}
					}
					break;
				}
				default:
					break;
			}
			return true;
		});
		auto springAt = [&springMap, W, H](std::int32_t tx, std::int32_t ty) -> std::uint8_t {
			return (tx >= 0 && ty >= 0 && tx < W && ty < H ? springMap[tx + ty * W] : 0);
		};
		auto springBoostAt = [&springBoost, W, H](std::int32_t tx, std::int32_t ty) -> std::int32_t {
			return (tx >= 0 && ty >= 0 && tx < W && ty < H ? springBoost[tx + ty * W] : 0);
		};
		auto isLift = [&liftMap, W, H](std::int32_t tx, std::int32_t ty) -> bool {
			return (tx >= 0 && ty >= 0 && tx < W && ty < H && liftMap[tx + ty * W] != 0);
		};
		auto isPole = [&liftMap, W, H](std::int32_t tx, std::int32_t ty) -> bool {
			return (tx >= 0 && ty >= 0 && tx < W && ty < H && liftMap[tx + ty * W] == 2);
		};
		auto onSurface = [&surfaceMap, W, H](std::int32_t tx, std::int32_t ty) -> bool {
			return (tx >= 0 && ty >= 0 && tx < W && ty < H && surfaceMap[tx + ty * W] != 0);
		};
		auto isTube = [&tubeMap, W, H](std::int32_t tx, std::int32_t ty) -> bool {
			return (tx >= 0 && ty >= 0 && tx < W && ty < H && tubeMap[tx + ty * W] != 0);
		};
		auto isHPole = [&hPoleMap, W, H](std::int32_t tx, std::int32_t ty) -> bool {
			return (tx >= 0 && ty >= 0 && tx < W && ty < H && hPoleMap[tx + ty * W] != 0);
		};
		auto isFlightPickup = [&flightMap, W, H](std::int32_t tx, std::int32_t ty) -> bool {
			return (tx >= 0 && ty >= 0 && tx < W && ty < H && flightMap[tx + ty * W] == 1);
		};

		// Collision at sub-tile precision. A whole tile only reads "free" or "solid", which walls off every slope,
		// half tile and the diagonal corridors built of them, so the player's movement is checked against 8x8 px
		// cells sampled from the tile masks instead. Destructible and trigger-controlled tiles stay passable (see
		// isFree() above), and vines and hooks read empty, as they do in the player's own collisions.
		constexpr std::int32_t TS = (std::int32_t)Tiles::TileSet::DefaultTileSize;
		constexpr std::int32_t CellSize = 8;
		constexpr std::int32_t CellsPerTile = TS / CellSize;
		constexpr std::uint32_t CellSolid = 0x01;	// Blocks the player from every side
		constexpr std::uint32_t CellFloor = 0x02;	// Holds up a player coming from above (solid or one-way)
		const std::int32_t CW = W * CellsPerTile, CH = H * CellsPerTile;
		// The 4x4 cells of a tile are packed into one word, 2 bits each, row by row
		std::unique_ptr<std::uint32_t[]> cells = std::make_unique<std::uint32_t[]>((std::size_t)totalTiles);
		for (std::int32_t ty = 0; ty < H; ty++) {
			for (std::int32_t tx = 0; tx < W; tx++) {
				if (tileMap->IsTileTrigger(tx, ty)) {
					// A trigger switches the tile between solid and empty - which way depends on the level: a wall
					// that opens, a platform that appears (a staircase of them, often). Either state has to be usable,
					// so the tile holds the player up on its top without blocking the way, like a one-way platform
					// (and a player standing on one can still drop through, once it opens - see getJumps())
					cells[tx + ty * W] = 0xAAu;
					continue;
				}
				if (tileMap->IsTileDestructible(tx, ty) || tileMap->IsTileEmpty(tx, ty)) {
					continue;
				}
				std::uint32_t& tileCells = cells[tx + ty * W];
				bool oneWay = tileMap->IsTileOneWay(tx, ty);
				if (!tileMap->IsTilePartiallySolid(tx, ty)) {
					// A filled mask - unless it belongs to a vine or a hook, which one sample tells
					if (!tileMap->IsTilePointEmpty(tx * TS + TS / 2, ty * TS + TS / 2, true)) {
						tileCells = (oneWay ? 0xAAAAAAAAu : 0xFFFFFFFFu);
					}
					continue;
				}
				// Sampled every 2 px: any sample makes a cell a floor, so a thin platform still holds the player up,
				// but it takes a quarter of them to make it a wall, so a slope doesn't narrow the corridor it forms
				for (std::int32_t cy = 0; cy < CellsPerTile; cy++) {
					for (std::int32_t cx = 0; cx < CellsPerTile; cx++) {
						std::int32_t solidSamples = 0;
						for (std::int32_t py = 1; py < CellSize; py += 2) {
							for (std::int32_t px = 1; px < CellSize; px += 2) {
								if (!tileMap->IsTilePointEmpty(tx * TS + cx * CellSize + px, ty * TS + cy * CellSize + py, true)) {
									solidSamples++;
								}
							}
						}
						std::uint32_t flags = 0;
						if (solidSamples > 0) {
							flags |= CellFloor;
						}
						if (!oneWay && solidSamples >= (CellSize / 2) * (CellSize / 2) / 4) {
							flags |= CellSolid;
						}
						tileCells |= flags << ((cy * CellsPerTile + cx) * 2);
					}
				}
			}
		}

		auto floorDiv = [](std::int32_t value, std::int32_t divisor) -> std::int32_t {
			return (value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor));
		};
		// Below the level is a floor only in a level whose pit is one to stand on, otherwise falling out kills
		// the player. TileMap tells which through a tile under the level, if one of the bottom tiles is empty.
		bool bottomIsFloor = true;
		for (std::int32_t tx = 0; tx < W; tx++) {
			if (tileMap->IsTileEmpty(tx, H - 1)) {
				bottomIsFloor = !tileMap->IsTileEmpty(tx, H);
				break;
			}
		}
		auto cellAt = [&](std::int32_t cx, std::int32_t cy) -> std::uint32_t {
			if (cx < 0 || cx >= CW) {
				return (CellSolid | CellFloor);	// The level's sides are walls
			}
			if (cy >= CH) {
				return (bottomIsFloor ? (CellSolid | CellFloor) : 0);
			}
			if (cy < 0) {
				cy = 0;
			}
			std::uint32_t tileCells = cells[(cx / CellsPerTile) + (cy / CellsPerTile) * W];
			return (tileCells >> (((cy % CellsPerTile) * CellsPerTile + (cx % CellsPerTile)) * 2)) & 0x03;
		};

		// The player's box, slightly smaller than the real one (22x30 px, see Player::OnUpdateHitbox()), so the
		// tracer rather finds a way that is a bit too tight than misses one. Positions are its feet, in pixels.
		constexpr std::int32_t BoxHalfWidth = 8, BoxHeight = 24;
		auto boxLeftCell = [&](std::int32_t x) { return floorDiv(x - BoxHalfWidth, CellSize); };
		auto boxRightCell = [&](std::int32_t x) { return floorDiv(x + BoxHalfWidth - 1, CellSize); };
		auto boxTopCell = [&](std::int32_t feetY) { return floorDiv(feetY - BoxHeight, CellSize); };
		auto boxBottomCell = [&](std::int32_t feetY) { return floorDiv(feetY - 1, CellSize); };
		auto cellsBlocked = [&](std::int32_t cx0, std::int32_t cx1, std::int32_t cy0, std::int32_t cy1) -> bool {
			for (std::int32_t cy = cy0; cy <= cy1; cy++) {
				for (std::int32_t cx = cx0; cx <= cx1; cx++) {
					if ((cellAt(cx, cy) & CellSolid) != 0) {
						return true;
					}
				}
			}
			return false;
		};
		auto boxBlocked = [&](std::int32_t x, std::int32_t feetY) -> bool {
			return cellsBlocked(boxLeftCell(x), boxRightCell(x), boxTopCell(feetY), boxBottomCell(feetY));
		};
		// Whether something holds up the box with its feet at the given height (a cell boundary)
		auto boxSupported = [&](std::int32_t x, std::int32_t feetY) -> bool {
			std::int32_t cy = floorDiv(feetY, CellSize);
			for (std::int32_t cx = boxLeftCell(x), cx1 = boxRightCell(x); cx <= cx1; cx++) {
				if ((cellAt(cx, cy) & CellFloor) != 0) {
					return true;
				}
			}
			return false;
		};

		// Where the player is in each tile: its highest position where the box fits and something holds it up, else
		// its lowest position where the box fits at all (in the air there), else -1.
		// A position belongs to the tile its body is centered in, not the one its feet are in, which is where the
		// level's events are placed - a spring on a floor a few pixels below a tile boundary is still in the tile
		// the player standing on that floor is in.
		//
		// The box is tried at several positions across the tile, not only at its middle, and the one it fits at is
		// kept in feetXMap. A gap exactly one tile wide that is offset half a tile from the grid - which is what a
		// passage between the solid halves of two tiles is - has nothing at either tile's middle and used to read
		// as solid rock on both sides, so the tracer walled itself out of whole sections: on `flash/06_medivo2`
		// that is the way out of the starting chamber, right above the vines and beside the warp, and with it
		// invisible the only route out was the warp itself.
		constexpr std::int32_t FirstFeetOffset = BoxHeight / 2 + CellSize / 2;	// Relative to the tile's top
		// Kept within the tile: at these offsets the body (16 px of the 32 a tile is wide) still lies inside it, so
		// a tile only counts as somewhere the player can be when they fit in the tile itself
		static const std::int32_t BodyOffsets[] = { 0, -CellSize, CellSize };
		std::unique_ptr<std::int32_t[]> feetMap = std::make_unique<std::int32_t[]>((std::size_t)totalTiles);
		std::unique_ptr<std::int8_t[]> feetXMap = std::make_unique<std::int8_t[]>((std::size_t)totalTiles);
		std::unique_ptr<std::uint8_t[]> standMap = std::make_unique<std::uint8_t[]>((std::size_t)totalTiles);
		for (std::int32_t ty = 0; ty < H; ty++) {
			for (std::int32_t tx = 0; tx < W; tx++) {
				std::int32_t feet = -1, feetX = 0;
				bool stands = false;
				for (std::int32_t offset : BodyOffsets) {
					std::int32_t x = tx * TS + TS / 2 + offset;
					for (std::int32_t r = 0; r < CellsPerTile; r++) {
						std::int32_t f = ty * TS + FirstFeetOffset + r * CellSize;
						if (!boxBlocked(x, f) && boxSupported(x, f)) {
							feet = f;
							feetX = offset;
							stands = true;
							break;
						}
					}
					if (stands) {
						break;
					}
				}
				if (!stands) {
					for (std::int32_t offset : BodyOffsets) {
						std::int32_t x = tx * TS + TS / 2 + offset;
						for (std::int32_t r = CellsPerTile - 1; r >= 0; r--) {
							std::int32_t f = ty * TS + FirstFeetOffset + r * CellSize;
							if (!boxBlocked(x, f)) {
								feet = f;
								feetX = offset;
								break;
							}
						}
						if (feet >= 0) {
							break;
						}
					}
				}
				feetMap[tx + ty * W] = feet;
				feetXMap[tx + ty * W] = (std::int8_t)feetX;
				standMap[tx + ty * W] = (stands ? 1 : 0);
			}
		}

		// Springs are actors, which fall onto the floor under their event, so they belong to the tile a player
		// standing on that floor is in, which can be the one below if the floor is a few pixels past a boundary
		for (std::int32_t ty = H - 1; ty >= 0; ty--) {
			for (std::int32_t tx = 0; tx < W; tx++) {
				std::int32_t i = tx + ty * W;
				if (springMap[i] == 0 || standMap[i] != 0) {
					continue;
				}
				for (std::int32_t f = ty * TS + FirstFeetOffset; f < (ty + 3) * TS; f += CellSize) {
					if (boxSupported(tx * TS + TS / 2, f)) {
						std::int32_t rty = floorDiv(f - BoxHeight / 2, TS);
						std::int32_t ri = tx + rty * W;
						if (rty > ty && rty < H && springMap[ri] == 0) {
							springMap[ri] = springMap[i];
							springBoost[ri] = springBoost[i];
							springMap[i] = 0;
							springBoost[i] = 0;
						}
						break;
					}
				}
			}
		}

		// Water: under the surface the player swims freely in every direction. "Set Water Level" events move the
		// surface while playing, so the highest surface the level ever gets counts - a route may need the water
		// the level raises to get somewhere.
		const std::int32_t waterTileY = (highestWater < (float)(H * TS) ? std::max<std::int32_t>(0, (std::int32_t)(highestWater / TS)) : INT32_MAX);
		auto inWater = [waterTileY](std::int32_t ty) -> bool {
			return (ty >= waterTileY);
		};

		// A tile the player can occupy: the box fits in it somewhere. Tube tiles count too, even if their terrain is
		// solid, since the tube transports the player through them, and so do lift tiles (vine/pole/hook/float-up),
		// which the player grabs and climbs, so they must not read as a ceiling that blocks the upward boost.
		auto occupiable = [&](std::int32_t tx, std::int32_t ty) -> bool {
			return (tx >= 0 && ty >= 0 && tx < W && ty < H && (feetMap[tx + ty * W] >= 0 || isTube(tx, ty) || isLift(tx, ty)));
		};
		// The player stands here if the ground holds up the box, or there's a bridge/platform or a spring (springs
		// are actors on otherwise-empty tiles, but the player rests on them)
		auto hasGround = [&](std::int32_t tx, std::int32_t ty) -> bool {
			return (tx >= 0 && ty >= 0 && tx < W && ty < H && standMap[tx + ty * W] != 0) || onSurface(tx, ty) || onSurface(tx, ty + 1)
				|| springAt(tx, ty) != 0 || springAt(tx, ty + 1) != 0;
		};
		auto feetAt = [&](std::int32_t tx, std::int32_t ty) -> std::int32_t {
			std::int32_t feet = feetMap[tx + ty * W];
			return (feet >= 0 ? feet : (ty + 1) * TS);
		};
		// Where across the tile the player's body is, which is its middle unless only an offset position fits
		auto bodyXAt = [&](std::int32_t tx, std::int32_t ty) -> std::int32_t {
			return tx * TS + TS / 2 + (tx >= 0 && ty >= 0 && tx < W && ty < H ? feetXMap[tx + ty * W] : 0);
		};
		// Whether the player can pass from a tile into the one beside it at this height. Two tiles can each have
		// room for the body and still have rock between them - that is what a body position away from the middle
		// of its tile means - so somewhere the body has to fit across the boundary as well, at any height within
		// the row. Walking and falling always tested this; the sideways step off a spring did not, and once the
		// body was allowed off-centre that let the route step clean through a wall two tiles thick
		// (`flash/06_medivo2`, straight out of the starting chamber at tiles 17-18).
		auto canCrossSideways = [&](std::int32_t tx, std::int32_t dir, std::int32_t ty) -> bool {
			std::int32_t edgeX = tx * TS + TS / 2 + dir * (TS / 2);
			for (std::int32_t r = 0; r < CellsPerTile; r++) {
				if (!boxBlocked(edgeX, ty * TS + FirstFeetOffset + r * CellSize)) {
					return true;
				}
			}
			return false;
		};
		// The tile the player's body is in, for a position of its feet
		auto tileOfFeet = [&](float feetY) -> std::int32_t {
			return floorDiv((std::int32_t)feetY - BoxHeight / 2, TS);
		};
		// The topmost tile the player's real hitbox reaches into at a tile, where it still touches the events -
		// a warp or an exit placed right above a floor that is a bit lower than the tile boundary is touched
		// by a player standing on that floor, although its body is in the tile below
		constexpr std::int32_t HitboxHeight = 30;
		auto topEventRow = [&](std::int32_t tx, std::int32_t ty) -> std::int32_t {
			return std::max<std::int32_t>(0, std::min(ty, floorDiv(feetAt(tx, ty) - HitboxHeight, TS)));
		};
		// The airspace an airboard, a copter or a fly carrot opens up: everything the player can fly to from one of
		// those pickups, flooded once here instead of per tile during the search. A Fly Off area is part of it -
		// the flight ends there and goes on on foot - but nothing is flown through it.
		std::unique_ptr<std::uint8_t[]> flightRegion = std::make_unique<std::uint8_t[]>((std::size_t)totalTiles);
		{
			std::queue<Vector2i> open;
			for (std::int32_t i = 0; i < totalTiles; i++) {
				if (flightMap[i] == 1 && occupiable(i % W, i / W)) {
					flightRegion[i] = 1;
					open.push(Vector2i(i % W, i / W));
				}
			}
			while (!open.empty()) {
				Vector2i f = open.front();
				open.pop();
				if (flightMap[f.X + f.Y * W] == 2) {
					continue;
				}
				for (std::int32_t dy = -1; dy <= 1; dy++) {
					for (std::int32_t dx = -1; dx <= 1; dx++) {
						std::int32_t nx = f.X + dx, ny = f.Y + dy;
						if ((dx == 0 && dy == 0) || !occupiable(nx, ny) || flightRegion[nx + ny * W] != 0) {
							continue;
						}
						// Diagonally only past two open corners
						if (dx != 0 && dy != 0 && (!occupiable(f.X + dx, f.Y) || !occupiable(f.X, f.Y + dy))) {
							continue;
						}
						flightRegion[nx + ny * W] = 1;
						open.push(Vector2i(nx, ny));
					}
				}
			}
		}

		// Snaps a tile to the nearest occupiable tile within a small radius (or {-1,-1} if none found)
		auto findSeed = [&occupiable](Vector2i t) -> Vector2i {
			if (occupiable(t.X, t.Y)) {
				return t;
			}
			for (std::int32_t r = 1; r <= 6; r++) {
				for (std::int32_t dy = -r; dy <= r; dy++) {
					for (std::int32_t dx = -r; dx <= r; dx++) {
						if (occupiable(t.X + dx, t.Y + dy)) {
							return Vector2i(t.X + dx, t.Y + dy);
						}
					}
				}
			}
			return Vector2i(-1, -1);
		};

		Vector2i spawnTile = findSeed(Vector2i((std::int32_t)(spawnPos.X / TS), (std::int32_t)(spawnPos.Y / TS)));
		if (spawnTile.X < 0) {
			LOGW("Cannot auto-place minimap track: spawn area is not walkable");
			return;
		}

		// Resolve each warp origin to a standable destination tile (matched by warp ID), so the search can teleport.
		// GetWarpTarget() returns the destination in world (pixel) coordinates, or (-1,-1) if the ID is unknown.
		HashMap<Vector2i, Vector2i, TileCoordHash> warpJump;
		for (const auto& origin : warpOrigins) {
			Vector2f targetWorld = eventMap->GetWarpTarget(origin.second());
			if (targetWorld.X >= 0.0f && targetWorld.Y >= 0.0f) {
				Vector2i dest = findSeed(Vector2i((std::int32_t)(targetWorld.X / TS), (std::int32_t)(targetWorld.Y / TS)));
				if (dest.X >= 0) {
					warpJump[origin.first()] = dest;
				}
			}
		}

		// Boost envelope (in tiles) of springs, vines and poles, on top of the jumps and spring launches that are
		// simulated, see JumpProfiles below
		constexpr std::int32_t JumpHeight = 7;		// How high the player gets off a vine/hook in a normal jump
		constexpr std::int32_t JumpReachX = 10;		// Horizontal reach of a jump off a vine/hook/pole
		constexpr std::int32_t JumpDropY = 6;		// How far below the player can land when jumping off one
		constexpr std::int32_t BoostHeight = 64;	// Max tiles a spring/float-up can carry the player up a clear column
		constexpr std::int32_t BoostReachX = 3;		// Sideways reach when stepping off a vertical boost
		constexpr std::int32_t HSpringReachX = 16;	// Horizontal reach of a running jump off a spring (clears wide pits)
		constexpr std::int32_t PoleReachY = 8;		// How far a spinning pole carries the player to the next pole (kept modest so it doesn't vault whole sections)
		constexpr std::int32_t LiftGapTiles = 1;	// How many tiles a vine/hook/float-up may skip before the ride ends
		constexpr std::int32_t MaxWalkStep = 40;	// Height difference (px) walking handles between neighboring tiles - a 45-degree slope, and some

		// Finds a clear arc for a jump from (sx,sy) to (sx+dx,sy+dy) - rise in the start column, travel across at
		// the apex, then descend to the landing - without passing through solid tiles, trying higher apexes up to
		// maxUp to clear taller obstacles. Returns the apex row, or INT32_MAX if no clear arc exists. Used for the
		// boosts, and to draw jumps as arcs on the minimap.
		// Vine/hook/pole tiles often have a solid mask, but the player grabs them instead of bumping into them (see
		// occupiable() above), so they don't block an arc either - otherwise a hook right above the ground would read
		// as a ceiling and the player could never jump up to it
		// Judged the same way as every other move in the search, at sub-tile precision: a whole-tile test reads a
		// slope, a half tile or a passage that lies across a tile boundary as solid rock, and the arc through it
		// is then refused outright. On `flash/06_medivo2` that is what kept the player from jumping off the top
		// of the vines into the one-tile gap beside the warp - the only way out of the starting chamber that
		// isn't the warp - so the route had no choice but to take the warp.
		auto arcFree = [&](std::int32_t tx, std::int32_t ty) -> bool {
			return occupiable(tx, ty) || isLift(tx, ty);
		};
		auto jumpApex = [&](std::int32_t sx, std::int32_t sy, std::int32_t dx, std::int32_t dy, std::int32_t maxUp) -> std::int32_t {
			std::int32_t ex = sx + dx, ey = sy + dy;
			std::int32_t x0 = (sx < ex ? sx : ex), x1 = (sx < ex ? ex : sx);
			std::int32_t apexStart = (sy < ey ? sy : ey);
			for (std::int32_t apexY = apexStart; apexY >= sy - maxUp; apexY--) {
				bool ok = true;
				for (std::int32_t yy = sy - 1; yy >= apexY; yy--) {
					if (!arcFree(sx, yy)) { ok = false; break; }
				}
				if (!ok) {
					break; // ceiling above the start: a higher apex is impossible too
				}
				for (std::int32_t xx = x0; xx <= x1; xx++) {
					// Each tile has to have room, and the player has to be able to get from one into the next -
					// two tiles can both have room with rock between them (see canCrossSideways)
					if (!arcFree(xx, apexY) ||
						(xx > x0 && !isLift(xx, apexY) && !isLift(xx - 1, apexY) && !canCrossSideways(xx - 1, 1, apexY))) {
						ok = false;
						break;
					}
				}
				if (!ok) {
					continue; // wall at this height; try a higher apex
				}
				// Descending toward the landing: a one-way platform is solid from above, so the arc can't drop
				// down through it (the rise phase above may still pass up through one-way platforms)
				for (std::int32_t yy = apexY + 1; yy <= ey; yy++) {
					if (!arcFree(ex, yy) || isOneWay(ex, yy)) { ok = false; break; }
				}
				if (ok) {
					return apexY;
				}
			}
			return INT32_MAX;
		};
		auto jumpClear = [&jumpApex](std::int32_t sx, std::int32_t sy, std::int32_t dx, std::int32_t dy, std::int32_t maxUp) -> bool {
			return jumpApex(sx, sy, dx, dy, maxUp) != INT32_MAX;
		};

		// Plain jumps are simulated with the physics of the original game (px per tick, see Player::LegacyJumpSpeed
		// and its neighbors) against the cells: launched upwards at 10 px/tick plus a quarter of the horizontal
		// speed, a light gravity while the key is held on the way up and a heavier one once it's let go, a floaty
		// fall, and never more than 8 px/tick applied on either axis. A ceiling ends the rise and a wall ends the
		// horizontal motion, as they do for the player. Each profile is one way to jump - how fast to run, when to
		// let go of the key, how to steer in the air and which of the characters' own moves to use - and runs in
		// both directions. A cooperation player can pick any character, so a level's way through is only ever one
		// that all of them manage, whichever move each needs for it: Spaz and Lori jump a second time at the apex,
		// Jazz glides down on his ears.
		enum class JumpMove : std::uint8_t {
			None,
			DoubleJump,					// A second launch at 8 px/tick at the apex (see Player::LegacyDoubleJumpSpeed)
			Glide						// Falling at most 1 px/tick (see Player::LegacyCopterDescentSpeed)
		};
		struct JumpProfile {
			float SpeedX;				// Applied horizontal speed, px/tick
			float LaunchSpeedX;			// Horizontal speed the launch gets its boost from (a dash has 16 before the cap)
			std::int32_t ReleaseTick;	// When the jump key is let go, 0 = held all the way up
			std::int32_t SteerTick;		// When the horizontal speed changes to SteerSpeedX, 0 = never
			float SteerSpeedX;
			JumpMove Move;
		};
		static constexpr JumpProfile JumpProfiles[] = {
			{ 0.0f, 0.0f, 0, 0, 0.0f, JumpMove::None },			// Straight up
			{ 2.0f, 2.0f, 0, 0, 0.0f, JumpMove::None },
			{ 4.0f, 4.0f, 0, 0, 0.0f, JumpMove::None },
			{ 6.0f, 6.0f, 0, 0, 0.0f, JumpMove::None },
			{ 8.0f, 8.0f, 0, 0, 0.0f, JumpMove::None },			// Running
			{ 8.0f, 16.0f, 0, 0, 0.0f, JumpMove::None },		// Dashing
			{ 0.0f, 0.0f, 0, 14, 5.0f, JumpMove::None },		// Straight up, then over onto a ledge above
			{ 6.0f, 6.0f, 0, 10, 0.0f, JumpMove::None },		// Forward, then dropping down onto what's below
			{ 8.0f, 16.0f, 0, 22, -5.0f, JumpMove::None },		// Dashing, then back onto a ledge behind (typically after hitting a wall)
			{ 8.0f, 8.0f, 0, 18, -5.0f, JumpMove::None },
			{ 0.0f, 0.0f, 6, 0, 0.0f, JumpMove::None },			// Short hops, under low ceilings
			{ 4.0f, 4.0f, 6, 0, 0.0f, JumpMove::None },
			{ 8.0f, 8.0f, 6, 0, 0.0f, JumpMove::None },
			{ 0.0f, 0.0f, 0, 0, 0.0f, JumpMove::DoubleJump },	// The characters' own moves
			{ 4.0f, 4.0f, 0, 0, 0.0f, JumpMove::DoubleJump },
			{ 8.0f, 16.0f, 0, 0, 0.0f, JumpMove::DoubleJump },
			{ 0.0f, 0.0f, 0, 40, 5.0f, JumpMove::DoubleJump },
			{ 8.0f, 16.0f, 0, 40, -5.0f, JumpMove::DoubleJump },
			{ 4.0f, 4.0f, 0, 0, 0.0f, JumpMove::Glide },
			{ 8.0f, 16.0f, 0, 0, 0.0f, JumpMove::Glide }
		};
		constexpr float DoubleJumpSpeed = 8.0f, GlideDescentSpeed = 1.0f, PoleLaunchBonus = 15.625f;
		constexpr float HPoleLaunchBonus = 8.0f, HPoleMaxLaunch = 20.0f;
		constexpr float JumpBaseSpeed = 10.0f, JumpSpeedScale = 0.25f, SpeedCap = 8.0f;
		constexpr float RiseGravityHeld = 0.375f, RiseGravityReleased = 0.875f, RiseGravityReleasedNearApex = 0.625f, FallGravity = 0.125f;
		constexpr std::int32_t MaxJumpTicks = 120;	// A longer flight goes on as a plain fall
		constexpr std::int32_t TubeControlTicks = 16;	// How long a tube keeps the controls, see Player::TubeControlTime

		auto stepCost = [](std::int32_t dx, std::int32_t dy) -> std::int32_t {
			// Tenths of a tile travelled, so a jump costs what walking the same distance does
			return std::max<std::int32_t>(10, (std::int32_t)(10.0f * sqrtf((float)(dx * dx + dy * dy)) + 0.5f));
		};
		// A flight priced at the ground it covers is free, and a long one then beats every route that follows the
		// level: crossing a valley in one hop is a shorter line than walking down into it and out again. Jazz's
		// copter is the extreme case - it descends at about a pixel a tick while the run carries on at eight, so a
		// single glide sails 30 tiles across at nearly constant height, and the route sails with it, over the top
		// of the level rather than through it (reported on `monk/05_damn`, and as an unreachable jump shortcut on
		// `secretf/01_easter1`).
		//
		// So a flight is charged for how far it carries the player *sideways* beyond an ordinary jump. Sideways
		// only: dropping down a shaft is free however deep it is, because a fall costs the player nothing either,
		// while distance across the air is what a route has to earn. A hop over a pit stays what walking it costs,
		// a glide across half the level costs several times the ground it skips - taken when there is no other way
		// through, left alone when there is.
		constexpr std::int32_t FreeFlightTiles = 6;			// Reach of an ordinary running jump, charged as walking
		constexpr std::int32_t LongFlightPenalty = 30;		// Tenths of a tile charged for each tile beyond it
		auto flightCost = [&](std::int32_t dx, std::int32_t dy) -> std::int32_t {
			std::int32_t across = (dx < 0 ? -dx : dx);
			return stepCost(dx, dy) + std::max<std::int32_t>(0, across - FreeFlightTiles) * LongFlightPenalty;
		};
		// What taking a warp costs, in the same tenths of a tile. It used to be 10 - one tile, the cheapest
		// edge in the graph - which made a warp very nearly *free* however far it teleported, so the route
		// went through every secret warp it could find. Reported on `flash/06_medivo2`, where it detours
		// through secret warps to save almost nothing.
		//
		// The minimap is meant to show the track, not the fastest way through the level, so a warp has to be
		// worth a real detour before the line takes one: forty tiles of walking. A warp is not free to a player
		// even when it is short - it has to be found and entered, and it takes the route somewhere they cannot
		// see coming. The figure is a judgement rather than a measurement - there is nothing to measure it
		// against - so it is named here to be turned.
		constexpr std::int32_t WarpCost = 400;
		// How much of the level the geometry alone has to reach, in percent of what warps reach, for the route
		// to be laid out warp-free (see the turning point below). Three quarters leaves the levels that merely
		// hide a warp or two on real geometry, and keeps the ones actually built around warps working as before.
		constexpr std::int32_t NoWarpRegionPercent = 75;

		// A flight the player steers freely: the vertical motion is the one of trace(), but every horizontal position
		// the player could steer to is followed at once - a set of box positions 8 px apart, which spreads by one
		// position a tick (the applied-movement cap) as far as the walls let it. Positions that hit a ceiling drop
		// out, and those over a floor on the way down land there. Used for the long flights (springs, poles), which
		// leave time to steer into a gap that none of the few fixed ways to steer in trace() would find.
		auto steeredFlight = [&](float x, float feetY, float vy, float riseGravity, auto&& emit, auto&& emitAt) {
			// Box at position k is centered at k * CellSize, so it covers cells k - 1 and k
			auto blockedAt = [&](std::int32_t k, std::int32_t feet) -> bool {
				return cellsBlocked(k - 1, k, boxTopCell(feet), boxBottomCell(feet));
			};
			std::int32_t lo = (std::int32_t)x / CellSize, hi = lo;
			SmallVector<std::uint8_t, 0> alive, next;
			alive.push_back(1);
			SmallVector<std::int32_t, 0> emitted;
			auto emitOnce = [&](std::int32_t tx, std::int32_t ty) {
				std::int32_t ti = tx + ty * W;
				for (std::int32_t e : emitted) {
					if (e == ti) {
						return;
					}
				}
				emitted.push_back(ti);
				emit(tx, ty);
			};
			auto landAt = [&](std::int32_t k, float feet) {
				std::int32_t tx = floorDiv(k * CellSize, TS), ty = tileOfFeet(feet);
				if (tx >= 0 && ty >= 0 && tx < W && ty < H) {
					std::int32_t ti = tx + ty * W;
					for (std::int32_t e : emitted) {
						if (e == ti) {
							return;
						}
					}
					emitted.push_back(ti);
					emitAt((float)(k * CellSize), feet);
				}
			};

			for (std::int32_t t = 1; t <= MaxJumpTicks; t++) {
				// Spreading sideways, at most one position a tick, never into a wall
				std::int32_t feet = (std::int32_t)feetY;
				next.clear();
				next.resize_for_overwrite(hi - lo + 3);
				bool any = false;
				for (std::int32_t k = lo - 1; k <= hi + 1; k++) {
					auto aliveAt = [&](std::int32_t kk) { return (kk >= lo && kk <= hi && alive[kk - lo] != 0); };
					bool reach = aliveAt(k) || aliveAt(k - 1) || aliveAt(k + 1);
					next[k - lo + 1] = (reach && (aliveAt(k) || !blockedAt(k, feet)) ? 1 : 0);
					any |= (next[k - lo + 1] != 0);
				}
				lo--;
				hi++;
				std::swap(alive, next);
				if (!any) {
					return;
				}

				float stepY = std::clamp(vy, -SpeedCap, SpeedCap);
				if (stepY < 0.0f) {
					std::int32_t newFeet = (std::int32_t)(feetY + stepY);
					for (std::int32_t k = lo; k <= hi; k++) {
						if (alive[k - lo] != 0 && cellsBlocked(k - 1, k, boxTopCell(newFeet), boxTopCell(feet) - 1)) {
							alive[k - lo] = 0;	// Bumped its head, falling back is what the other flights are for
						}
					}
					feetY += stepY;
				} else if (stepY > 0.0f) {
					std::int32_t fromCell = floorDiv(feet + CellSize - 1, CellSize);
					std::int32_t toCell = floorDiv((std::int32_t)(feetY + stepY), CellSize);
					for (std::int32_t k = lo; k <= hi; k++) {
						if (alive[k - lo] == 0) {
							continue;
						}
						for (std::int32_t cy = fromCell; cy <= toCell; cy++) {
							if (boxSupported(k * CellSize, cy * CellSize) && !blockedAt(k, cy * CellSize)) {
								landAt(k, (float)(cy * CellSize));
								alive[k - lo] = 0;
								break;
							}
						}
					}
					feetY += stepY;
					if (feetY > (float)(H * TS)) {
						return;
					}
				}
				vy = (vy < 0.0f ? vy + riseGravity : std::min(vy + FallGravity, SpeedCap));

				// What the hitbox touches on the way can be grabbed, and water ends the flight
				std::int32_t top = floorDiv((std::int32_t)feetY - HitboxHeight, TS), bottom = floorDiv((std::int32_t)feetY - 1, TS);
				std::int32_t bodyRow = tileOfFeet(feetY);
				for (std::int32_t k = lo; k <= hi; k++) {
					if (alive[k - lo] == 0) {
						continue;
					}
					std::int32_t tx = floorDiv(k * CellSize, TS);
					if (onSurface(tx, bodyRow) && vy > 0.0f) {
						landAt(k, feetY);
						alive[k - lo] = 0;
						continue;
					}
					if (bodyRow >= 0 && inWater(bodyRow)) {
						emitOnce(tx, bodyRow);
						alive[k - lo] = 0;
						continue;
					}
					for (std::int32_t ty = top; ty <= bottom; ty++) {
						if (isLift(tx, ty) || isHPole(tx, ty) || isTube(tx, ty) || isFlightPickup(tx, ty)) {
							emitOnce(tx, ty);
						}
					}
				}

				// Keeping the window around the positions still flying
				while (lo < hi && alive[0] == 0) {
					alive.erase(alive.begin());
					lo++;
				}
				while (hi > lo && alive[hi - lo] == 0) {
					alive.pop_back();
					hi--;
				}
			}
			// Still in the air, it goes on as a fall from there
			for (std::int32_t k = lo; k <= hi; k += CellsPerTile) {
				if (alive[k - lo] != 0) {
					landAt(k, feetY);
				}
			}
		};

		// Flies the player from a position at a velocity until it lands, grabs something or runs out of time, and
		// reports where it got: emitAt() with the position it landed at, emit() with a tile it grabbed onto.
		// The jump key is held on the way up until releaseTick (0 = all the way up), at steerTick the horizontal
		// speed changes to steerVx (0 = never), and `move` is a character's own move used on the way.
		// Pole relaunches already followed by a steered flight in the flights being simulated (tile, speed)
		SmallVector<Pair<std::int32_t, std::int32_t>, 0> steeredPoles;
		// Whether everything that holds up the box at the given height is a trigger tile's top
		auto onlyTriggerFloor = [&](std::int32_t x, std::int32_t feetY) -> bool {
			std::int32_t cy = floorDiv(feetY, CellSize);
			for (std::int32_t cx = boxLeftCell(x), cx1 = boxRightCell(x); cx <= cx1; cx++) {
				if ((cellAt(cx, cy) & CellFloor) != 0 && !tileMap->IsTileTrigger(floorDiv(cx, CellsPerTile), floorDiv(cy, CellsPerTile))) {
					return false;
				}
			}
			return true;
		};
		auto trace = [&](float x, float feetY, float vx, float vy, std::int32_t releaseTick, std::int32_t steerTick, float steerVx,
			JumpMove move, bool offSpring, auto&& emit, auto&& emitAt, bool throughTriggers = false) {
			bool onPole = false, poleLaunched = false, onHPole = false;
			// A horizontal pole's launch is travelled in full, above the applied-movement cap, while it carries
			float capX = SpeedCap;
			std::int32_t hPoleCarryLeft = 0;
			// A pole takes the horizontal speed away while it spins the player, who steers the same way again after
			const float steerAfterPole = (steerTick > 0 ? steerVx : vx);
			std::int32_t poleSteerTick = 0;
			for (std::int32_t t = 1; t <= MaxJumpTicks; t++) {
				if (steerTick > 0 && t == steerTick) {
					vx = steerVx;
				}
				if (poleSteerTick > 0 && t == poleSteerTick) {
					vx = steerAfterPole;
				}
				if (hPoleCarryLeft > 0 && --hPoleCarryLeft == 0) {
					capX = SpeedCap;
				}
				// The box was clear where it was, so only the cells it moves into need to be checked
				if (vx != 0.0f) {
					float nx = x + std::clamp(vx, -capX, capX);
					std::int32_t cy0 = boxTopCell((std::int32_t)feetY), cy1 = boxBottomCell((std::int32_t)feetY);
					bool blocked = (vx > 0.0f
						? cellsBlocked(boxRightCell((std::int32_t)x) + 1, boxRightCell((std::int32_t)nx), cy0, cy1)
						: cellsBlocked(boxLeftCell((std::int32_t)nx), boxLeftCell((std::int32_t)x) - 1, cy0, cy1));
					if (blocked) {
						vx = 0.0f;
					} else {
						x = nx;
					}
				}
				float stepY = std::clamp(vy, -SpeedCap, SpeedCap);
				if (stepY < 0.0f) {
					if (cellsBlocked(boxLeftCell((std::int32_t)x), boxRightCell((std::int32_t)x), boxTopCell((std::int32_t)(feetY + stepY)), boxTopCell((std::int32_t)feetY) - 1)) {
						vy = 0.0f;
					} else {
						feetY += stepY;
					}
				} else if (stepY > 0.0f) {
					// Land on the first floor the feet cross on the way down
					std::int32_t fromCell = floorDiv((std::int32_t)feetY + CellSize - 1, CellSize);
					std::int32_t toCell = floorDiv((std::int32_t)(feetY + stepY), CellSize);
					for (std::int32_t cy = fromCell; cy <= toCell; cy++) {
						if (boxSupported((std::int32_t)x, cy * CellSize) && !boxBlocked((std::int32_t)x, cy * CellSize) &&
							!(throughTriggers && onlyTriggerFloor((std::int32_t)x, cy * CellSize))) {
							emitAt(x, (float)(cy * CellSize));
							return;
						}
					}
					feetY += stepY;
					if (onSurface(floorDiv((std::int32_t)x, TS), tileOfFeet(feetY))) {
						// A bridge or a moving platform, which the cells don't know about
						emitAt(x, feetY);
						return;
					}
					if (feetY > (float)(H * TS)) {
						return;
					}
				}
				if (vy < 0.0f) {
					if (poleLaunched) {
						vy += (offSpring ? RiseGravityHeld : RiseGravityReleased);
					} else {
						vy += (releaseTick == 0 || t < releaseTick ? RiseGravityHeld
							: (-vy > 1.0f ? RiseGravityReleased : RiseGravityReleasedNearApex));
					}
				} else if (move == JumpMove::DoubleJump) {
					vy = -DoubleJumpSpeed;
					move = JumpMove::None;
				} else {
					vy = std::min(vy + FallGravity, (move == JumpMove::Glide ? GlideDescentSpeed : SpeedCap));
				}
				// Grabbing a vine or a hook or getting sucked into a tube, as soon as the real hitbox touches one (the
				// hands reach a hook well above where the body is), or diving into water. A vertical pole doesn't
				// hold the player, it spins them and throws them on the way they were going, at the speed they came
				// with plus 15.625 px/tick (see Player::LegacyPoleLaunchBonus) - poles in a row and a spring into a
				// pole compound.
				std::int32_t ctx = floorDiv((std::int32_t)x, TS), cty = tileOfFeet(feetY);
				bool touchesPole = false, touchesHPole = false;
				for (std::int32_t ty = floorDiv((std::int32_t)feetY - HitboxHeight, TS); ty <= floorDiv((std::int32_t)feetY - 1, TS); ty++) {
					if (isHPole(ctx, ty)) {
						// The way the player was going (or facing), at the speed it came with plus 8 px/tick, at most 20
						// (see Player::LegacyHPoleLaunchBonus), carried for 60 ticks
						if (!onHPole && vx != 0.0f) {
							emit(ctx, ty);
							vx = (vx < 0.0f ? -1.0f : 1.0f) * std::min(std::abs(vx) + HPoleLaunchBonus, HPoleMaxLaunch);
							vy = 0.0f;
							feetY = (float)(ty * TS + TS / 2 + BoxHeight / 2);
							capX = HPoleMaxLaunch;
							hPoleCarryLeft = 60;
						}
						touchesHPole = true;
					} else if (isPole(ctx, ty)) {
						if (!onPole && vy != 0.0f) {
							emit(ctx, ty);
							vy = (vy < 0.0f ? -1.0f : 1.0f) * (PoleLaunchBonus + std::abs(vy));
							vx = 0.0f;
							x = (float)(ctx * TS + TS / 2);
							poleLaunched = true;
							poleSteerTick = t + 10;
							// Out of a pole the player has all the time to steer anywhere, see steeredFlight()
							Pair<std::int32_t, std::int32_t> key = pair(ctx + ty * W, (std::int32_t)vy);
							bool steered = false;
							for (const auto& p : steeredPoles) {
								steered |= (p.first() == key.first() && p.second() == key.second());
							}
							if (!steered) {
								steeredPoles.push_back(key);
								steeredFlight(x, feetY, vy, (offSpring ? RiseGravityHeld : RiseGravityReleased), emit, emitAt);
							}
						}
						touchesPole = true;
					} else if (isLift(ctx, ty) || isTube(ctx, ty) || isFlightPickup(ctx, ty)) {
						emit(ctx, ty);
						return;
					}
				}
				onPole = touchesPole;
				onHPole = touchesHPole;
				if (cty >= 0 && inWater(cty)) {
					emit(ctx, cty);
					return;
				}
			}
			// Still in the air, it goes on as a fall from there
			emitAt(x, feetY);
		};

		// Where the flights (jumps, tube exits) from a tile lead, as tile index + cost, simulated once when the tile
		// is first expanded and kept for the later searches, which differ only in which tiles they may enter
		// Jumps are cached for every tile in arrays, tube exits (a tube's last tile only) in a map
		struct FlightCache {
			std::unique_ptr<std::int32_t[]> Start;		// First entry of each tile, -1 until simulated
			std::unique_ptr<std::uint16_t[]> Count;
			HashMap<std::int32_t, Pair<std::int32_t, std::int32_t>> Sparse;
			SmallVector<Pair<std::int32_t, std::int32_t>, 0> Entries;
		};
		FlightCache jumpFlights;
		jumpFlights.Start = std::make_unique<std::int32_t[]>((std::size_t)totalTiles);
		jumpFlights.Count = std::make_unique<std::uint16_t[]>((std::size_t)totalTiles);
		for (std::int32_t i = 0; i < totalTiles; i++) {
			jumpFlights.Start[i] = -1;
		}
		FlightCache tubeFlights;
		FlightCache hPoleFlights;
		// `priced` charges the long flights extra, see flightCost() - the player's own jumps and glides are
		// priced, a tube, a horizontal pole or an airboard is not: those carry the player because the level
		// says so, and a route is meant to follow them
		auto getFlights = [&](FlightCache& cache, Vector2i c, bool priced, auto&& simulate) -> ArrayView<const Pair<std::int32_t, std::int32_t>> {
			std::int32_t ci = c.X + c.Y * W;
			if (cache.Start == nullptr) {
				auto it = cache.Sparse.find(ci);
				if (it != cache.Sparse.end()) {
					return { cache.Entries.data() + it->second.first(), (std::size_t)it->second.second() };
				}
			}
			if (cache.Start == nullptr || cache.Start[ci] < 0) {
				std::int32_t first = (std::int32_t)cache.Entries.size();
				steeredPoles.clear();
				auto emit = [&](std::int32_t tx, std::int32_t ty) {
					if (!occupiable(tx, ty)) {
						return;
					}
					std::int32_t ti = tx + ty * W;
					if (ti == ci) {
						return;
					}
					// A flood of a flight adds every tile once already, and the lists are short otherwise
					if ((std::int32_t)cache.Entries.size() - first < 64) {
						for (std::int32_t k = first; k < (std::int32_t)cache.Entries.size(); k++) {
							if (cache.Entries[k].first() == ti) {
								return;
							}
						}
					}
					cache.Entries.push_back(pair(ti, priced ? flightCost(tx - c.X, ty - c.Y) : stepCost(tx - c.X, ty - c.Y)));
				};
				// The landing tile is where the body is, or the neighbor on the side the player is at, when the box
				// centered in that tile wouldn't fit
				auto emitAt = [&](float x, float feetY) {
					std::int32_t tx = floorDiv((std::int32_t)x, TS), ty = tileOfFeet(feetY);
					if (tx < 0 || ty < 0 || tx >= W || ty >= H) {
						return;
					}
					if (!occupiable(tx, ty)) {
						tx += ((std::int32_t)x - tx * TS < TS / 2 ? -1 : 1);
					}
					emit(tx, ty);
				};
				simulate(emit, emitAt);
				std::int32_t count = (std::int32_t)cache.Entries.size() - first;
				if (cache.Start == nullptr) {
					cache.Sparse.emplace(ci, pair(first, count));
					return { cache.Entries.data() + first, (std::size_t)count };
				}
				cache.Start[ci] = first;
				cache.Count[ci] = (std::uint16_t)std::min<std::int32_t>(count, UINT16_MAX);
			}
			return { cache.Entries.data() + cache.Start[ci], (std::size_t)cache.Count[ci] };
		};
		auto getJumps = [&](Vector2i c) {
			return getFlights(jumpFlights, c, true, [&](auto&& emit, auto&& emitAt) {
				const float startX = (float)bodyXAt(c.X, c.Y), startFeet = (float)feetAt(c.X, c.Y);
				for (const auto& p : JumpProfiles) {
					for (float dir = -1.0f; dir <= 1.0f; dir += 2.0f) {
						if (dir > 0.0f && p.SpeedX == 0.0f && p.SteerSpeedX == 0.0f) {
							continue;	// A straight jump is the same in both directions
						}
						trace(startX, startFeet, p.SpeedX * dir, -(JumpBaseSpeed + p.LaunchSpeedX * JumpSpeedScale),
							p.ReleaseTick, p.SteerTick, p.SteerSpeedX * dir, p.Move, false, emit, emitAt);
					}
				}

				// Stepping off a ledge part of the way across the tile - a gap that is offset from the tile grid (between
				// the solid halves of two tiles) is never under the middle of a tile, which is where the other moves
				// start from, but the player walks over it and drops in all the same. So does a player standing on a
				// trigger floor, which may be the way on once the trigger opens it (and so may the ones below it).
				for (std::int32_t offset : { 0, -8, 8, -16, 16 }) {
					std::int32_t x = (std::int32_t)startX + offset, feet = (std::int32_t)startFeet;
					bool clear = true;
					for (std::int32_t sx = (std::int32_t)startX; clear && sx != x; sx += (offset > 0 ? CellSize : -CellSize)) {
						clear = !boxBlocked(sx + (offset > 0 ? CellSize : -CellSize), feet);
					}
					if (!clear) {
						continue;
					}
					bool supported = boxSupported(x, feet);
					if (supported && !((feet % CellSize) == 0 && onlyTriggerFloor(x, feet))) {
						continue;
					}
					if (supported) {
						feet += CellSize;	// Through the trigger floor
						if (boxBlocked(x, feet)) {
							continue;
						}
					} else if (offset == 0) {
						continue;	// In the air already, falling is handled by the search
					}
					trace((float)x, (float)feet, 0.0f, 0.0f, 0, 0, 0.0f, JumpMove::None, false, emit, emitAt, true);
					if (offset != 0) {
						trace((float)x, (float)feet, (offset > 0 ? 2.0f : -2.0f), 0.0f, 0, 0, 0.0f, JumpMove::None, false, emit, emitAt, true);
					}
				}

				// A spring sets the speed outright, 16, 24 or 32 px/tick by its color (see Spring::OnActivatedAsync()),
				// upwards, or sideways - which way is only resolved when the level runs, so both are tried. The player
				// steers as with a jump.
				std::int32_t springY = (springAt(c.X, c.Y) != 0 ? c.Y : (springAt(c.X, c.Y + 1) != 0 ? c.Y + 1 : -1));
				if (springY >= 0) {
					std::int32_t boost = springBoostAt(c.X, springY);
					float strength = (boost <= 10 ? 16.0f : (boost >= 20 ? 32.0f : 24.0f));
					for (float dir = -1.0f; dir <= 1.0f; dir += 2.0f) {
						if (springAt(c.X, springY) == 1) {
							for (float speedX : { 0.0f, 2.0f, 4.0f, 6.0f, 8.0f }) {
								if (dir > 0.0f && speedX == 0.0f) {
									continue;
								}
								trace(startX, startFeet, speedX * dir, -strength, 0, 0, 0.0f, JumpMove::None, true, emit, emitAt);
							}
							trace(startX, startFeet, 0.0f, -strength, 0, 20, 5.0f * dir, JumpMove::None, true, emit, emitAt);
							if (dir < 0.0f) {
								steeredFlight(startX, startFeet, -strength, RiseGravityHeld, emit, emitAt);
							}
						} else {
							trace(startX, startFeet, strength * dir, 0.0f, 0, 0, 0.0f, JumpMove::None, true, emit, emitAt);
							trace(startX, startFeet, strength * dir, -JumpBaseSpeed, 0, 0, 0.0f, JumpMove::None, true, emit, emitAt);
						}
					}
				}
			});
		};

		// Search over the tiles recording predecessors, so a route can be reconstructed. Dijkstra, weighted by the
		// distance travelled, so a long jump costs what walking as far does and the route is the shortest one.
		// 'blocked' optionally forbids tiles (used to force the second pass around the other arm of the loop).
		// 'useWarps' toggles warp teleport edges: enabled for reachability, disabled when tracing a route so it
		// follows the geometry (e.g., climbs poles) rather than teleporting past it.
		auto runSearch = [&](Vector2i start, const std::uint8_t* blocked, std::int32_t* parent, std::int32_t* dist, Vector2i& farTile, std::int32_t& visitedCount, bool useWarps) {
			using QueueItem = std::pair<std::int32_t, std::int32_t>;	// Distance, tile index
			std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> q;
			std::int32_t startIdx = start.X + start.Y * W;
			dist[startIdx] = 0;
			farTile = start;
			std::int32_t farDist = 0;
			visitedCount = 0;
			q.push(QueueItem(0, startIdx));

			auto relax = [&](std::int32_t ni, std::int32_t cost, std::int32_t fromDist, std::int32_t fromIdx) {
				if (blocked != nullptr && blocked[ni] != 0) {
					return;
				}
				std::int32_t nd = fromDist + cost;
				if (dist[ni] >= 0 && dist[ni] <= nd) {
					return;
				}
				dist[ni] = nd;
				parent[ni] = fromIdx;
				q.push(QueueItem(nd, ni));
			};
			auto tryAdd = [&](std::int32_t tx, std::int32_t ty, std::int32_t cost, std::int32_t fromDist, std::int32_t fromIdx) {
				if (occupiable(tx, ty)) {
					relax(tx + ty * W, cost, fromDist, fromIdx);
				}
			};
			// Adds a tile without the occupiable check (the caller verified it can be entered, e.g., a warp target)
			auto tryAddRaw = [&](std::int32_t tx, std::int32_t ty, std::int32_t cost, std::int32_t fromDist, std::int32_t fromIdx) {
				if (tx >= 0 && ty >= 0 && tx < W && ty < H) {
					relax(tx + ty * W, cost, fromDist, fromIdx);
				}
			};

			while (!q.empty()) {
				QueueItem item = q.top();
				q.pop();
				std::int32_t ci = item.second;
				std::int32_t cd = item.first;
				if (cd != dist[ci]) {
					continue;	// A shorter way to this tile has been found since
				}
				visitedCount++;
				Vector2i c(ci % W, ci / W);

				bool grounded = hasGround(c.X, c.Y);
				bool lift = isLift(c.X, c.Y);
				bool tube = isTube(c.X, c.Y);
				bool water = inWater(c.Y);
				// A spinning pole flings the player in the direction of entry momentum (see Player::NextPoleStage):
				// up if they rose into it, down if they descended into it (or entered from the side, where gravity
				// dominates). Approximate the entry direction from how the search arrived, so a pole on a downward
				// section doesn't launch the route up and over it.
				bool poleHere = isPole(c.X, c.Y);
				bool poleUp = poleHere && (parent[ci] >= 0 && (parent[ci] / W) > c.Y);
				bool poleDown = poleHere && !poleUp;

				// Only consider real footing (not transient air tiles above a spring/jump) as the farthest point
				if (cd > farDist && (grounded || lift || tube || water)) {
					farDist = cd;
					farTile = c;
				}

				if (tube) {
					// Inside a tube the player is transported through the connected passage, ignoring gravity and
					// tight geometry. At its end the player flies on in its direction - through whatever the tube
					// graphics' masks are, many tubes turn the player's collisions off (Become No-clip) - until
					// there's room for it.
					std::int32_t code = tubeMap[ci] - 1;
					std::int32_t sx = (code % 3) - 1, sy = (code / 3) - 1;
					tryAdd(c.X - 1, c.Y, 10, cd, ci);
					tryAdd(c.X + 1, c.Y, 10, cd, ci);
					tryAdd(c.X, c.Y - 1, 10, cd, ci);
					tryAdd(c.X, c.Y + 1, 10, cd, ci);
					if ((sx != 0 || sy != 0) && !isTube(c.X + sx, c.Y + sy)) {
						auto exits = getFlights(tubeFlights, c, false, [&](auto&& emit, auto&& emitAt) {
							for (std::int32_t k = 1; k <= 12; k++) {
								std::int32_t nx = c.X + sx * k, ny = c.Y + sy * k;
								if (nx < 0 || ny < 0 || nx >= W || ny >= H) {
									break;
								}
								if (feetMap[nx + ny * W] >= 0) {
									// Out of the tube at its speed, and steering once the tube lets go of the controls
									emit(nx, ny);
									const float x = (float)bodyXAt(nx, ny), feet = (float)feetMap[nx + ny * W];
									const float vx = (float)tubeSpeedX[ci], vy = (float)tubeSpeedY[ci];
									for (float steer : { 0.0f, -6.0f, 6.0f }) {
										trace(x, feet, vx, vy, 0, (steer != 0.0f ? TubeControlTicks : 0), steer, JumpMove::None, false, emit, emitAt);
									}
									break;
								}
							}
						});
						for (const auto& exit : exits) {
							relax(exit.first(), exit.second(), cd, ci);
						}
					}
				}
				if (water) {
					// Under water the player swims
					tryAdd(c.X - 1, c.Y, 10, cd, ci);
					tryAdd(c.X + 1, c.Y, 10, cd, ci);
					tryAdd(c.X, c.Y - 1, 10, cd, ci);
					tryAdd(c.X, c.Y + 1, 10, cd, ci);
					tryAdd(c.X - 1, c.Y - 1, 14, cd, ci);
					tryAdd(c.X + 1, c.Y - 1, 14, cd, ci);
					tryAdd(c.X - 1, c.Y + 1, 14, cd, ci);
					tryAdd(c.X + 1, c.Y + 1, 14, cd, ci);
				}

				// Dropping or walking onto a horizontal pole, which flings the player either way it faces - flights
				// through one are handled by trace()
				std::int32_t hPoleY = -1;
				for (std::int32_t ey = topEventRow(c.X, c.Y); ey <= c.Y && hPoleY < 0; ey++) {
					if (isHPole(c.X, ey)) {
						hPoleY = ey;
					}
				}
				if (hPoleY >= 0) {
					auto flights = getFlights(hPoleFlights, c, false, [&](auto&& emit, auto&& emitAt) {
						const float x = (float)(c.X * TS + TS / 2), feet = (float)(hPoleY * TS + TS / 2 + BoxHeight / 2);
						for (float speedX : { -8.0f, -0.01f, 0.01f, 8.0f }) {
							trace(x, feet, speedX, 0.0f, 0, 0, 0.0f, JumpMove::None, false, emit, emitAt);
						}
					});
					for (const auto& flight : flights) {
						relax(flight.first(), flight.second(), cd, ci);
					}
				}

				// With an airboard or a copter the player flies freely through the open space the pickup opens up,
				// until a Fly Off area takes it away (a Reforged copter runs out after 10 seconds, but that's far
				// enough anyway). Moved through a tile at a time, like swimming: the flight used to be one edge from
				// the pickup to every tile it could reach, which the minimap then drew as a straight line from the
				// pickup across whatever stood in the way - reported on `prince/06_labrat2`, where the line cut
				// 39 tiles diagonally through the middle of the level from the airboard at [134, 32]. Tile by tile
				// the route follows the air the player actually flies through, and a long flight costs what it
				// really travels instead of the straight line between its ends.
				if (flightRegion[ci] != 0) {
					for (std::int32_t dy = -1; dy <= 1; dy++) {
						for (std::int32_t dx = -1; dx <= 1; dx++) {
							if (dx == 0 && dy == 0) {
								continue;
							}
							std::int32_t nx = c.X + dx, ny = c.Y + dy;
							if (nx < 0 || ny < 0 || nx >= W || ny >= H || flightRegion[nx + ny * W] == 0) {
								continue;
							}
							// Diagonally only past two open corners
							if (dx != 0 && dy != 0 && (!occupiable(c.X + dx, c.Y) || !occupiable(c.X, c.Y + dy))) {
								continue;
							}
							relax(nx + ny * W, (dx != 0 && dy != 0 ? 14 : 10), cd, ci);
						}
					}
				}

				// A warp teleports the player to its destination; continue tracing the route from there
				if (useWarps) {
					for (std::int32_t ey = topEventRow(c.X, c.Y); ey <= c.Y; ey++) {
						auto warp = warpJump.find(Vector2i(c.X, ey));
						if (warp != warpJump.end()) {
							tryAddRaw(warp->second.X, warp->second.Y, WarpCost, cd, ci);
						}
					}
				}

				std::uint8_t springHere = (springAt(c.X, c.Y) != 0 ? springAt(c.X, c.Y) : springAt(c.X, c.Y + 1));
				// Springs and vines/hooks/float-up always carry upward; a pole only when the player rose into it
				bool boostUp = springHere == 1 || (lift && !poleHere) || poleUp;

				if (boostUp) {
					// Vertical springs launch the player a fixed height (per spring type); a vine, a hook or a
					// float-up area carries them as far as it reaches. Ride upward (bounded by that cap or a
					// ceiling) and step off onto any ledge within reach along the way.
					// Poles only carry a modest distance (they don't lift the player the whole shaft like a float-up
					// area or vine), so cap them low; springs use their per-type height
					std::int32_t cap = poleHere ? PoleReachY : (lift ? BoostHeight : springBoostAt(c.X, c.Y));
					if (cap == 0) {
						cap = springBoostAt(c.X, c.Y + 1);
					}
					if (cap <= 0 || cap > BoostHeight) {
						cap = BoostHeight;
					}
					// A lift used to ride the whole clear column above it, which turns one vine tile into a lift to
					// the roof of the level: on `flash/05_medivo1` a handful of scattered vines in a shaft carried the
					// route 40 tiles up and along the top of the level, where the player can climb nowhere near that
					// high. What a vine or a float-up area actually covers is its own tiles, so the ride follows them
					// and stops where they stop - with a tile of slack, because the events are often laid out with
					// gaps in them, and with the jump off the top handled separately below.
					std::int32_t liftGap = 0;
					for (std::int32_t k = 1; k <= cap; k++) {
						std::int32_t ty = c.Y - k;
						if (!occupiable(c.X, ty)) {
							break; // ceiling
						}
						if (lift && !poleHere) {
							liftGap = (isLift(c.X, ty) ? 0 : liftGap + 1);
							if (liftGap > LiftGapTiles) {
								break; // the vine/hook/float-up ends here
							}
						}
						tryAdd(c.X, ty, 10 * k, cd, ci);
						for (std::int32_t dir = -1; dir <= 1; dir += 2) {
							for (std::int32_t s = 1; s <= BoostReachX; s++) {
								std::int32_t sx = c.X + dir * s;
								if (!occupiable(sx, ty)) {
									break; // wall blocks stepping further sideways at this height
								}
								// ...and so does rock between two tiles that each have room, unless one of them is
								// a vine or a pole, which the player grabs through whatever mask it is drawn on
								if (!isLift(sx, ty) && !isLift(sx - dir, ty) && !canCrossSideways(sx - dir, dir, ty)) {
									break;
								}
								if (hasGround(sx, ty) || isLift(sx, ty)) {
									tryAdd(sx, ty, 10 * k + 10 * s, cd, ci);
								}
							}
						}
					}
				}

				if (poleDown) {
					// Flung downward: descend the pole's column (then keeps falling below it via the airborne logic);
					// never drop down through a one-way platform (solid from above)
					if (!isOneWay(c.X, c.Y + 1)) {
						tryAdd(c.X, c.Y + 1, 10, cd, ci);
					}
				}

				if (lift) {
					// On a vine/pole the player can also move sideways, and let go of a vine or a hook to drop down
					tryAdd(c.X - 1, c.Y, 10, cd, ci);
					tryAdd(c.X + 1, c.Y, 10, cd, ci);
					if (!poleHere) {
						tryAdd(c.X, c.Y + 1, 10, cd, ci);
					}
				}

				// A jump-off arc: from a spring (long arc, height by type), or off a vine/pole (a normal jump - the
				// player can climb then leap to a forward ledge). jumpClear picks a feasible apex under any ceiling,
				// so the player arcs forward instead of going straight up into it.
				if ((grounded && (springHere == 1 || springHere == 2)) || (lift && !poleDown)) {
					std::int32_t sMaxUp;
					std::int32_t maxDx;
					if (springHere == 1) {
						std::int32_t sc = springBoostAt(c.X, c.Y);
						if (sc == 0) {
							sc = springBoostAt(c.X, c.Y + 1);
						}
						sMaxUp = (sc > 0 ? sc : JumpHeight);
						maxDx = HSpringReachX;
					} else if (springHere == 2) {
						sMaxUp = JumpHeight;
						maxDx = HSpringReachX;
					} else if (poleHere) {
						// Spinning pole (entered ascending): carries the player a modest distance up to the next
						// pole/ledge - kept conservative so it doesn't vault over whole sections
						sMaxUp = PoleReachY;
						maxDx = JumpReachX;
					} else {
						// Vine/hook/float-up: a normal jump off it
						sMaxUp = JumpHeight;
						maxDx = JumpReachX;
					}
					for (std::int32_t dx = -maxDx; dx <= maxDx; dx++) {
						if (dx == 0) {
							continue; // straight up is already covered by the column boost
						}
						// The spring/climb provides the height and running provides the horizontal distance
						// independently, so a wide gap can still end on a higher ledge; jumpClear validates the arc.
						for (std::int32_t dy = -sMaxUp; dy <= JumpDropY; dy++) {
							std::int32_t tx = c.X + dx, ty = c.Y + dy;
							if ((hasGround(tx, ty) || isLift(tx, ty)) && occupiable(tx, ty) && jumpClear(c.X, c.Y, dx, dy, sMaxUp)) {
								tryAdd(tx, ty, stepCost(dx, dy), cd, ci);
							}
						}
					}
				}

				const std::int32_t feet = feetAt(c.X, c.Y);
				if (!grounded && !lift && !tube && !water) {
					// Airborne: falling, drifting sideways as it falls (the side it drifts to has to be open at the
					// current height), or landing next to where it is
					tryAdd(c.X, c.Y + 1, 10, cd, ci);
					for (std::int32_t dir = -1; dir <= 1; dir += 2) {
						std::int32_t edgeX = c.X * TS + TS / 2 + dir * (TS / 2);
						if (boxBlocked(edgeX, feet)) {
							continue;
						}
						if (hasGround(c.X + dir, c.Y)) {
							tryAdd(c.X + dir, c.Y, 10, cd, ci);
						}
						tryAdd(c.X + dir, c.Y + 1, 14, cd, ci);
						if (occupiable(c.X + dir, c.Y + 1) && !boxBlocked(edgeX + dir * TS, feet + TS)) {
							tryAdd(c.X + 2 * dir, c.Y + 1, 22, cd, ci);
						}
					}
				}

				if (grounded || (water && c.Y == waterTileY)) {
					// Walking to either side, up or down a slope - the standing heights of the two tiles may differ
					// by a slope's worth, and the box has to fit where they meet, at the higher of the two. Walking
					// off a ledge goes on as a fall from the tile next to it.
					for (std::int32_t dir = -1; dir <= 1; dir += 2) {
						std::int32_t nx = c.X + dir;
						if (nx < 0 || nx >= W) {
							continue;
						}
						std::int32_t edgeX = c.X * TS + TS / 2 + dir * (TS / 2);
						bool walked = false;
						for (std::int32_t k : { 0, -1, 1 }) {
							std::int32_t ny = c.Y + k;
							if (ny < 0 || ny >= H || feetMap[nx + ny * W] < 0 || !hasGround(nx, ny)) {
								continue;
							}
							std::int32_t toFeet = feetMap[nx + ny * W];
							if (std::abs(toFeet - feet) > MaxWalkStep || boxBlocked(edgeX, std::min(toFeet, feet))) {
								continue;
							}
							tryAdd(nx, ny, (k == 0 ? 10 : 14), cd, ci);
							walked = true;
							break;
						}
						if (!walked && !boxBlocked(edgeX, feet)) {
							tryAdd(nx, c.Y, 10, cd, ci);
						}
					}

					// Jumping, simulated (see JumpProfiles)
					for (const auto& jump : getJumps(c)) {
						relax(jump.first(), jump.second(), cd, ci);
					}
				}
			}
		};

		auto reconstruct = [&](const std::int32_t* parent, std::int32_t fromIdx, std::int32_t toIdx, SmallVector<Vector2i, 0>& out) {
			SmallVector<Vector2i, 0> rev;
			std::int32_t cur = toIdx;
			while (cur >= 0) {
				rev.push_back(Vector2i(cur % W, cur / W));
				if (cur == fromIdx) {
					break;
				}
				cur = parent[cur];
			}
			for (std::int32_t i = (std::int32_t)rev.size() - 1; i >= 0; i--) {
				out.push_back(rev[i]);
			}
		};

		// First pass: spawn -> everything; find the farthest reachable tile (far side of the loop)
		std::unique_ptr<std::int32_t[]> parent = std::make_unique<std::int32_t[]>((std::size_t)totalTiles);
		std::unique_ptr<std::int32_t[]> dist = std::make_unique<std::int32_t[]>((std::size_t)totalTiles);
		for (std::int32_t i = 0; i < totalTiles; i++) { parent[i] = -1; dist[i] = -1; }

		Vector2i farTile;
		std::int32_t regionSize = 0;
		runSearch(spawnTile, nullptr, parent.get(), dist.get(), farTile, regionSize, true);

		if (regionSize < 32) {
			LOGW("Cannot auto-place minimap track: walkable region is too small ({} tiles)", regionSize);
			return;
		}
		// A lap needs a loop to trace, which an arena doesn't have. A route to the level exit is meaningful in an
		// open level too, it only has to reach the exit (checked below).
		if (routeType == TrackRouteType::Lap && regionSize > (totalTiles * 3) / 5) {
			LOGW("Cannot auto-place minimap track: level looks like an open arena, not a track ({} of {} tiles walkable)", regionSize, totalTiles);
			return;
		}

		// The same search again with the warps taken out. It settles both arms below - the route follows the geometry
		// rather than teleporting past it - but first it settles the *turning point*, which matters more.
		//
		// The far tile above was found with warps enabled, so on a level whose warps reach a pocket the geometry
		// doesn't, the farthest tile lands in that pocket. Nothing can then be traced to it warp-free, so both arms
		// fall back to warps and the track threads every secret door on the way. That is the reported
		// `flash/06_medivo2`, where the detour through the secret warps saves almost nothing: the shortcut was never
		// really about distance, it was that the destination had been picked through a warp in the first place.
		//
		// So when the level is essentially walkable - the warp-free search reaching most of what the warp-enabled one
		// does - the turning point comes from the warp-free search and the whole track follows real geometry. This
		// also keeps the far tile clear of the warp cost below, which inflates every distance measured past a warp and
		// would otherwise drag the turning point towards whatever lies beyond one. A level genuinely built around its
		// warps, where dropping them strands most of the map, keeps the warp-enabled far tile and is still shown end
		// to end.
		std::unique_ptr<std::int32_t[]> parentNW = std::make_unique<std::int32_t[]>((std::size_t)totalTiles);
		std::unique_ptr<std::int32_t[]> distNW = std::make_unique<std::int32_t[]>((std::size_t)totalTiles);
		for (std::int32_t i = 0; i < totalTiles; i++) { parentNW[i] = -1; distNW[i] = -1; }
		Vector2i farNW;
		std::int32_t regionNW = 0;
		runSearch(spawnTile, nullptr, parentNW.get(), distNW.get(), farNW, regionNW, false);

		if (farNW.X >= 0 && regionNW * 100 >= regionSize * NoWarpRegionPercent && distNW[farNW.X + farNW.Y * W] > 0) {
			farTile = farNW;
		}

		const std::int32_t spawnIdx = spawnTile.X + spawnTile.Y * W;
		std::int32_t farIdx = farTile.X + farTile.Y * W;

		// The finish of a lap is the "Set Lap" warp, or - if the level has none - a level-exit event (end-of-level
		// area or EOL sign), mirroring how Race mode itself falls back to level exits for lap completion. A full lap
		// goes spawn -> (out to the far side) -> finish; routing via the far tile forces the trace around the whole
		// loop even when the start line sits right next to the finish (so there's no short path to block). A route
		// to the level exit considers the exits, and the boss if no exit can be reached. Among the candidates, take
		// the one reachable from spawn and farthest along the track, so the route spans the level.
		// A marker counts as reached from the tiles below it down to the floor - bosses and signs are actors, which
		// fall onto the floor under their event, and an area is touched by a player standing a little under it
		// (see topEventRow()) - in its column, or in the one next to it for a marker that is part of a wider area
		auto pickFarthestReachable = [&](const SmallVector<Vector2i, 0>& markers) -> Vector2i {
			Vector2i best(-1, -1);
			std::int32_t bestDist = -1;
			for (const auto& m : markers) {
				Vector2i t(-1, -1);
				for (std::int32_t dx : { 0, -1, 1 }) {
					std::int32_t nx = m.X + dx;
					for (std::int32_t ny = m.Y; ny <= m.Y + 4 && nx >= 0 && nx < W && ny >= 0 && ny < H; ny++) {
						std::int32_t ni = nx + ny * W;
						if (!occupiable(nx, ny)) {
							break;
						}
						if (dist[ni] >= 0) {
							t = Vector2i(nx, ny);
							break;
						}
						if (standMap[ni] != 0) {
							break;
						}
					}
					if (t.X >= 0) {
						break;
					}
				}
				if (t.X < 0) {
					t = findSeed(m);
				}
				if (t.X >= 0) {
					std::int32_t d = dist[t.X + t.Y * W];
					if (d > bestDist) { bestDist = d; best = t; }
				}
			}
			return best;
		};
		Vector2i finishTile = (routeType == TrackRouteType::Lap ? pickFarthestReachable(startMarkers) : Vector2i(-1, -1));
		const char* finishSource = "warp";
		if (finishTile.X < 0) {
			finishTile = pickFarthestReachable(exitMarkers);
			finishSource = "exit";
		}
		if (routeType == TrackRouteType::LevelExit && (finishTile.X < 0 || dist[finishTile.X + finishTile.Y * W] < 0)) {
			finishTile = pickFarthestReachable(bossMarkers);
			finishSource = "boss";
		}
		if (finishTile.X < 0) {
			finishSource = "far";
		}
		bool finishReachable = (finishTile.X >= 0 && dist[finishTile.X + finishTile.Y * W] >= 0);
		Vector2i target = farTile;
		if (finishReachable) {
			target = finishTile;
			// If the finish itself lies far from the spawn (a point-to-point race, not a loop back to the start),
			// make it the turnaround too, so the route runs straight to it instead of overshooting to the farthest
			// tile and doubling back past the finish. A route to the level exit always runs straight to it.
			std::int32_t finDist = dist[finishTile.X + finishTile.Y * W];
			if (routeType == TrackRouteType::LevelExit || (dist[farIdx] > 0 && finDist * 2 >= dist[farIdx])) {
				farTile = finishTile;
				farIdx = finishTile.X + finishTile.Y * W;
			}
		}
		const std::int32_t targetIdx = target.X + target.Y * W;

		std::int32_t springUp = 0, springSide = 0, liftCount = 0, surfaceCount = 0, poleCount = 0;
		std::int32_t springUpReached = 0, liftReached = 0, poleReached = 0, flightCount = 0, tubeCount = 0, hPoleCount = 0;
		for (std::int32_t i = 0; i < totalTiles; i++) {
			if (springMap[i] == 1) { springUp++; if (dist[i] >= 0) { springUpReached++; } }
			else if (springMap[i] == 2) { springSide++; }
			if (liftMap[i] != 0) { liftCount++; if (dist[i] >= 0) { liftReached++; } }
			if (liftMap[i] == 2) { poleCount++; if (dist[i] >= 0) { poleReached++; } }
			if (surfaceMap[i] != 0) { surfaceCount++; }
			if (flightMap[i] == 1) { flightCount++; }
			if (tubeMap[i] != 0) { tubeCount++; }
			if (hPoleMap[i] != 0) { hPoleCount++; }
		}
		LOGI("Minimap track geometry: {} vertical springs ({} reached) + {} horizontal, {} lift ({} reached, of which {} poles {} reached), {} surface, {} flight pickup(s), {} tube, {} horizontal pole(s), {} warp(s), {} exit(s), {} boss(es); finish [{}, {}] via {} reachable={} (dist {}); without warps {} tiles, far [{}, {}]",
			springUp, springUpReached, springSide, liftCount, liftReached, poleCount, poleReached, surfaceCount,
			flightCount, tubeCount, hPoleCount, (std::int32_t)warpJump.size(), (std::int32_t)exitMarkers.size(),
			(std::int32_t)bossMarkers.size(),
			finishTile.X, finishTile.Y, finishSource,
			(finishTile.X >= 0 && dist[finishTile.X + finishTile.Y * W] >= 0) ? 1 : 0,
			(finishTile.X >= 0 ? dist[finishTile.X + finishTile.Y * W] : -1),
			regionNW, farNW.X, farNW.Y);
		// Each resolved warp and whether the search actually reached its origin (so it could teleport) - helps diagnose
		// warps the tracer stops at instead of following
		for (const auto& wj : warpJump) {
			std::int32_t oi = wj.first.X + wj.first.Y * W;
			LOGI("Minimap track warp: origin [{}, {}] (reached={}) -> dest [{}, {}] (reached={})",
				wj.first.X, wj.first.Y, (dist[oi] >= 0) ? 1 : 0,
				wj.second.X, wj.second.Y, (dist[wj.second.X + wj.second.Y * W] >= 0) ? 1 : 0);
		}

		if (routeType == TrackRouteType::LevelExit && !finishReachable && (!exitMarkers.empty() || !bossMarkers.empty())) {
			// Without the exit there's nothing to lead the players to - the farthest tile could be any dead end.
			// A level that has no exit or boss at all (a bonus stage ends by its timer) is shown out to its far
			// end instead, like a lap.
			LOGW("Cannot auto-place minimap track: no reachable level exit (spawn [{}, {}], region {} tiles)", spawnTile.X, spawnTile.Y, regionSize);
			return;
		}

		// The route is traced in two arms, spawn -> via -> finish, where `via` is the turning point. The first arm
		// prefers a warp-free route so the line follows the geometry (e.g., climbs the poles or vines) instead of
		// teleporting past it via a warp shortcut, and falls back to the warp-enabled route only when the turning
		// point can't be reached without warps (e.g., a section only accessible by a warp). The second arm takes
		// the opposite side by blocking the first; if blocking disconnects the finish (e.g., wide corridors) it is
		// retried unblocked so the route still reaches the end, and when the first arm already ends at the finish
		// there's no second arm to trace (which saves a whole search on a big level).
		bool routeNoWarp = true;
		auto traceRouteVia = [&](const SmallVector<Vector2i, 0>& vias, bool mustComplete, SmallVector<Vector2i, 0>& route) -> bool {
			std::unique_ptr<std::uint8_t[]> blocked = std::make_unique<std::uint8_t[]>((std::size_t)totalTiles);
			std::unique_ptr<std::int32_t[]> parent2 = std::make_unique<std::int32_t[]>((std::size_t)totalTiles);
			std::unique_ptr<std::int32_t[]> dist2 = std::make_unique<std::int32_t[]>((std::size_t)totalTiles);
			SmallVector<Vector2i, 0> arm;
			Vector2i cur = spawnTile;
			bool noWarp = true, firstArm = true;
			route.clear();

			for (std::int32_t w = 0; w <= (std::int32_t)vias.size(); w++) {
				Vector2i dest = (w < (std::int32_t)vias.size() ? vias[w] : target);
				std::int32_t destIdx = dest.X + dest.Y * W;
				if (destIdx == cur.X + cur.Y * W) {
					continue;
				}

				arm.clear();
				if (firstArm) {
					// The way out of the spawn is already searched, both with warps and without
					noWarp = (distNW[destIdx] >= 0);
					reconstruct(noWarp ? parentNW.get() : parent.get(), spawnIdx, destIdx, arm);
					if (arm.size() < 2) {
						return false;
					}
				} else {
					for (std::int32_t i = 0; i < totalTiles; i++) { parent2[i] = -1; dist2[i] = -1; }
					Vector2i far2;
					std::int32_t visited2 = 0;
					runSearch(cur, blocked.get(), parent2.get(), dist2.get(), far2, visited2, !noWarp);
					if (dist2[destIdx] < 0) {
						// Blocking what the line already covers cut the rest of it off; retry unblocked so the
						// route still gets there, and only then fall back to the warps
						for (std::int32_t i = 0; i < totalTiles; i++) { parent2[i] = -1; dist2[i] = -1; }
						runSearch(cur, nullptr, parent2.get(), dist2.get(), far2, visited2, !noWarp);
					}
					if (dist2[destIdx] < 0 && noWarp) {
						for (std::int32_t i = 0; i < totalTiles; i++) { parent2[i] = -1; dist2[i] = -1; }
						runSearch(cur, nullptr, parent2.get(), dist2.get(), far2, visited2, true);
					}
					if (dist2[destIdx] < 0) {
						if (mustComplete) {
							return false;
						}
						break;	// The line ends where it got to
					}
					reconstruct(parent2.get(), cur.X + cur.Y * W, destIdx, arm);
					if (arm.size() < 2) {
						continue;
					}
				}

				// The next arm takes the other side of what this one covers, which is what keeps a lap a loop
				// rather than the same corridor twice
				for (std::int32_t i = 1; i + 1 < (std::int32_t)arm.size(); i++) {
					blocked[arm[i].X + arm[i].Y * W] = 1;
				}
				for (std::int32_t i = (route.empty() ? 0 : 1); i < (std::int32_t)arm.size(); i++) {
					route.push_back(arm[i]);
				}
				cur = dest;
				firstArm = false;
			}

			// If the way out overshoots just past the finish (e.g., a catch-spring sits a couple of tiles beyond
			// the finish warp), end the route at the finish instead of looping out to it and back. Only with a
			// single turning point - once the line is laid out through several sections, passing near the finish
			// on the way is what it is meant to do.
			if (vias.size() <= 1 && target.X == finishTile.X && target.Y == finishTile.Y) {
				for (std::int32_t i = (std::int32_t)route.size() / 2; i < (std::int32_t)route.size(); i++) {
					std::int32_t ddx = route[i].X - finishTile.X, ddy = route[i].Y - finishTile.Y;
					if (std::max(ddx < 0 ? -ddx : ddx, ddy < 0 ? -ddy : ddy) <= 2) {
						route.erase(route.begin() + (i + 1), route.end());
						Vector2i last = route[route.size() - 1];
						if (last.X != finishTile.X || last.Y != finishTile.Y) {
							route.push_back(finishTile);
						}
						break;
					}
				}
			}
			if (route.size() < 2) {
				return false;
			}
			routeNoWarp = noWarp;
			return true;
		};

		SmallVector<Vector2i, 0> vias, route;
		if (farTile.X != target.X || farTile.Y != target.Y) {
			vias.push_back(farTile);
		}
		if (!traceRouteVia(vias, false, route)) {
			LOGW("Cannot auto-place minimap track: could not trace a route through the level");
			return;
		}

		// The route so far is the cheapest way from the spawn to the finish, and the cheapest way is not the track.
		// A level whose start sits next to a spring or a set of poles lets the line climb straight out and run to
		// the exit along the top, leaving the whole section the player is meant to run first untouched - reported
		// on `monk/05_damn`, where the track should sweep east along the floor, ride the spring at its end and come
		// back west along the shelf before it climbs, and the traced line instead turned round at the spawn and
		// went up. The two are both walkable; the one the level is built around is simply longer, so no pricing of
		// edges will ever choose it.
		//
		// What tells them apart is coverage. Measure how far each reachable tile is from the line - in tiles, along
		// the walkable region, so a pocket behind a wall is far away even where it looks close - and gather what is
		// left over into the sections it forms. A section big enough to be a part of the level rather than a corner
		// of it is one the line is missing, so the line is laid out through it as well, and the whole is measured
		// again. A lab like `prince/06_labrat2` has its rooms spread over half a dozen of these and needs several
		// passes before the line runs where the level does; a route that already covers its level gains nothing on
		// the first pass and is left exactly as it was.
		//
		// The turning points are kept in the order the player reaches them, nearest first, which is the order a
		// track is run in - and each arm is traced around what the ones before it already cover, so the line comes
		// back along the other side instead of retracing itself.
		constexpr std::int32_t CoverageDetourTiles = 10;		// How far off the line a tile has to be to count as missed
		constexpr std::int32_t CoverageDetourPercent = 10;		// How much of the level has to be off the line to lay it out again
		constexpr std::int32_t MinCoverageSection = 48;			// How big a missed section has to be to be worth going through
		constexpr std::int32_t MaxCoverageDetours = 4;			// How many of them the line is laid out through at most
		constexpr std::int32_t MaxCoverageTries = 3;			// How many sections are tried per pass before giving up
		{
			std::unique_ptr<std::int32_t[]> offRoute = std::make_unique<std::int32_t[]>((std::size_t)totalTiles);
			std::unique_ptr<std::uint8_t[]> section = std::make_unique<std::uint8_t[]>((std::size_t)totalTiles);
			std::queue<std::int32_t> open;
			SmallVector<Vector2i, 0> candidate, covered;
			SmallVector<Pair<std::int32_t, Vector2i>, 0> candidates;

			for (std::int32_t pass = 0; pass < MaxCoverageDetours; pass++) {
				for (std::int32_t i = 0; i < totalTiles; i++) { offRoute[i] = -1; section[i] = 0; }
				for (const auto& r : route) {
					std::int32_t ri = r.X + r.Y * W;
					if (offRoute[ri] < 0) { offRoute[ri] = 0; open.push(ri); }
				}
				while (!open.empty()) {
					std::int32_t ci = open.front();
					open.pop();
					std::int32_t cx = ci % W, cy = ci / W;
					for (std::int32_t k = 0; k < 4; k++) {
						std::int32_t nx = cx + (k == 0 ? -1 : k == 1 ? 1 : 0), ny = cy + (k == 2 ? -1 : k == 3 ? 1 : 0);
						if (nx < 0 || ny < 0 || nx >= W || ny >= H) { continue; }
						std::int32_t ni = nx + ny * W;
						if (offRoute[ni] >= 0 || dist[ni] < 0) { continue; }
						offRoute[ni] = offRoute[ci] + 1;
						open.push(ni);
					}
				}

				std::int32_t missed = 0;
				candidates.clear();
				for (std::int32_t seed = 0; seed < totalTiles; seed++) {
					if (offRoute[seed] < CoverageDetourTiles || section[seed] != 0) {
						continue;
					}
					std::int32_t size = 0, off = 0;
					Vector2i best(-1, -1);
					section[seed] = 1;
					open.push(seed);
					while (!open.empty()) {
						std::int32_t ci = open.front();
						open.pop();
						size++;
						// A turning point has to be somewhere the player can stand, or the line ends in mid-air
						if (offRoute[ci] > off && (standMap[ci] != 0 || liftMap[ci] != 0 || tubeMap[ci] != 0)) {
							off = offRoute[ci];
							best = Vector2i(ci % W, ci / W);
						}
						std::int32_t cx = ci % W, cy = ci / W;
						for (std::int32_t k = 0; k < 4; k++) {
							std::int32_t nx = cx + (k == 0 ? -1 : k == 1 ? 1 : 0), ny = cy + (k == 2 ? -1 : k == 3 ? 1 : 0);
							if (nx < 0 || ny < 0 || nx >= W || ny >= H) { continue; }
							std::int32_t ni = nx + ny * W;
							if (section[ni] != 0 || offRoute[ni] < CoverageDetourTiles) { continue; }
							section[ni] = 1;
							open.push(ni);
						}
					}
					missed += size;
					if (best.X >= 0 && size >= MinCoverageSection) {
						candidates.push_back(pair(size, best));
					}
				}
				// Enough of the level has to be off the line for it to be worth laying out again
				if (candidates.empty() || missed * 100 < regionSize * CoverageDetourPercent) {
					break;
				}
				// Biggest section first, and on to the next one when the line cannot be traced through it - the
				// point farthest from the line can be a dead end that nothing leads on from
				std::sort(candidates.begin(), candidates.end(), [](const Pair<std::int32_t, Vector2i>& a, const Pair<std::int32_t, Vector2i>& b) {
					return a.first() > b.first();
				});
				bool laidOut = false;
				for (std::int32_t c = 0; c < (std::int32_t)candidates.size() && c < MaxCoverageTries && !laidOut; c++) {
					// Reached in the order the player reaches them
					candidate = vias;
					candidate.push_back(candidates[c].second());
					std::sort(candidate.begin(), candidate.end(), [&](Vector2i a, Vector2i b) {
						return dist[a.X + a.Y * W] < dist[b.X + b.Y * W];
					});
					if (!traceRouteVia(candidate, true, covered)) {
						continue;
					}
					LOGI("Auto-placing minimap track: {} of {} tiles lay {}+ tiles off the route, the biggest section {} of them; laying it out through [{}, {}] as well",
						missed, regionSize, CoverageDetourTiles, candidates[c].first(), candidates[c].second().X, candidates[c].second().Y);
					vias = candidate;
					route = covered;
					farTile = vias[vias.size() - 1];
					farIdx = farTile.X + farTile.Y * W;
					laidOut = true;
				}
				if (!laidOut) {
					break;
				}
			}
		}

		// Expand jumps (non-adjacent steps) into up-over-down arcs so the minimap draws the player going up and
		// over an obstacle instead of a straight line cutting through it. A parallel group id is bumped at each
		// warp edge (origin -> teleport target) so the minimap doesn't draw a line straight across the level.
		SmallVector<Vector2i, 0> routeArc;
		SmallVector<std::uint8_t, 0> routeArcGroup;
		std::uint8_t curGroup = 0;
		routeArc.push_back(route[0]);
		routeArcGroup.push_back(curGroup);
		for (std::int32_t i = 1; i < (std::int32_t)route.size(); i++) {
			Vector2i a = route[i - 1], b = route[i];
			auto w = warpJump.find(a);
			if (w != warpJump.end() && w->second.X == b.X && w->second.Y == b.Y) {
				if (curGroup < 255) {
					curGroup++; // teleport: break the line here
				}
			} else {
				std::int32_t ddx = b.X - a.X, ddy = b.Y - a.Y;
				std::int32_t adx = (ddx < 0 ? -ddx : ddx), ady = (ddy < 0 ? -ddy : ddy);
				if (adx > 1 || ady > 1) {
					// The drawn arc is looked for the way the search itself decides what the player can pass
					// through, at sub-tile precision: jumpApex() is deliberately stricter, because it also
					// *validates* jumps, and a step it refuses here left the line as a straight diagonal from
					// take-off to landing - which is what drew it through the walls on `prince/06_labrat2`. Where
					// the two disagree it is the search that was right: the step is in the route because a path
					// through those tiles exists, so the line has to be drawn going round rather than not at all.
					std::int32_t apexY = jumpApex(a.X, a.Y, ddx, ddy, BoostHeight);
					std::int32_t topY = (a.Y < b.Y ? a.Y : b.Y);
					if (apexY != INT32_MAX && apexY < topY) {
						routeArc.push_back(Vector2i(a.X, apexY));
						routeArcGroup.push_back(curGroup);
						routeArc.push_back(Vector2i(b.X, apexY));
						routeArcGroup.push_back(curGroup);
					}
				}
			}
			routeArc.push_back(b);
			routeArcGroup.push_back(curGroup);
		}

		// Whatever is still drawn across solid rock is re-drawn through the open space around it. The arc above is
		// an up-over-down corner, which cannot describe a flight that rises and travels at once - a spring launch
		// is a parabola, and trace() finds it by simulating the physics, not by any shape the minimap can draw - so
		// a step like that kept its straight line from take-off to landing and cut through everything between
		// (`prince/06_labrat2`). Since the step is in the route, a way through does exist; searching the open tiles
		// between its ends finds one, and drawing that keeps the line inside the level even where it cannot look
		// like the jump it stands for.
		{
			constexpr std::int32_t MaxDetourVisited = 6000;	// A line long enough to need more than this is left alone
			constexpr std::int32_t DetourMargin = 24;		// How far outside the step's own box the way round may go
			std::unique_ptr<std::int32_t[]> from = std::make_unique<std::int32_t[]>((std::size_t)totalTiles);
			auto lineOpen = [&](Vector2i p, Vector2i q) -> bool {
				std::int32_t steps = std::max(std::abs(q.X - p.X), std::abs(q.Y - p.Y));
				for (std::int32_t s = 1; s < steps; s++) {
					if (!occupiable(p.X + (q.X - p.X) * s / steps, p.Y + (q.Y - p.Y) * s / steps)) {
						return false;
					}
				}
				return true;
			};
			SmallVector<Vector2i, 0> fixedArc;
			SmallVector<std::uint8_t, 0> fixedGroup;
			SmallVector<Vector2i, 0> detour;
			fixedArc.push_back(routeArc[0]);
			fixedGroup.push_back(routeArcGroup[0]);
			for (std::int32_t i = 1; i < (std::int32_t)routeArc.size(); i++) {
				Vector2i a = routeArc[i - 1], b = routeArc[i];
				bool crosses = false;
				if (routeArcGroup[i] == routeArcGroup[i - 1]) {
					std::int32_t steps = std::max(std::abs(b.X - a.X), std::abs(b.Y - a.Y));
					for (std::int32_t s = 1; s < steps && !crosses; s++) {
						crosses = !occupiable(a.X + (b.X - a.X) * s / steps, a.Y + (b.Y - a.Y) * s / steps);
					}
				}
				if (crosses) {
					std::int32_t minX = std::max<std::int32_t>(0, std::min(a.X, b.X) - DetourMargin);
					std::int32_t maxX = std::min<std::int32_t>(W - 1, std::max(a.X, b.X) + DetourMargin);
					std::int32_t minY = std::max<std::int32_t>(0, std::min(a.Y, b.Y) - DetourMargin);
					std::int32_t maxY = std::min<std::int32_t>(H - 1, std::max(a.Y, b.Y) + DetourMargin);
					std::int32_t startIdx2 = a.X + a.Y * W, targetIdx2 = b.X + b.Y * W, visited = 0;
					std::queue<std::int32_t> open;
					open.push(startIdx2);
					from[startIdx2] = -1;	// Marked, no predecessor
					bool found = false;
					while (!open.empty() && visited < MaxDetourVisited && !found) {
						std::int32_t ci2 = open.front();
						open.pop();
						visited++;
						std::int32_t cx = ci2 % W, cy = ci2 / W;
						for (std::int32_t dy = -1; dy <= 1 && !found; dy++) {
							for (std::int32_t dx = -1; dx <= 1; dx++) {
								std::int32_t nx = cx + dx, ny = cy + dy;
								if ((dx == 0 && dy == 0) || nx < minX || ny < minY || nx > maxX || ny > maxY) {
									continue;
								}
								std::int32_t ni = nx + ny * W;
								if (from[ni] != 0 || !occupiable(nx, ny)) {
									continue;
								}
								if (dx != 0 && dy != 0 && (!occupiable(cx + dx, cy) || !occupiable(cx, cy + dy))) {
									continue;	// Diagonally only past two open corners
								}
								from[ni] = ci2 + 1;
								if (ni == targetIdx2) { found = true; break; }
								open.push(ni);
							}
						}
					}
					if (found) {
						detour.clear();
						for (std::int32_t cur = targetIdx2; cur != startIdx2; cur = from[cur] - 1) {
							detour.push_back(Vector2i(cur % W, cur / W));
						}
						std::reverse(detour.begin(), detour.end());
						detour.insert(detour.begin(), a);
						// The path came out of a grid search, so it is a staircase. Pull it taut - keep only the
						// corners it needs to stay inside the open tiles - or the line is a flight of steps and
						// every step of it becomes a checkpoint.
						std::int32_t last = (std::int32_t)detour.size() - 1;
						for (std::int32_t k = 0; k < last; ) {
							std::int32_t next = k + 1;
							for (std::int32_t m = last; m > k + 1; m--) {
								if (lineOpen(detour[k], detour[m])) {
									next = m;
									break;
								}
							}
							fixedArc.push_back(detour[next]);
							fixedGroup.push_back(routeArcGroup[i]);
							k = next;
						}
					}
					// The marks only ever cover the searched box, so clearing that is enough
					for (std::int32_t y = minY; y <= maxY; y++) {
						for (std::int32_t x = minX; x <= maxX; x++) { from[x + y * W] = 0; }
					}
					if (found) {
						continue;
					}
				}
				fixedArc.push_back(b);
				fixedGroup.push_back(routeArcGroup[i]);
			}
			routeArc = std::move(fixedArc);
			routeArcGroup = std::move(fixedGroup);
		}

		// Minimap extent = the route's bounding box (padded for track width), plus start markers
		Vector2i boundsMin(W, H), boundsMax(-1, -1);
		auto includeBounds = [&boundsMin, &boundsMax](Vector2i t) {
			if (t.X < boundsMin.X) { boundsMin.X = t.X; }
			if (t.Y < boundsMin.Y) { boundsMin.Y = t.Y; }
			if (t.X > boundsMax.X) { boundsMax.X = t.X; }
			if (t.Y > boundsMax.Y) { boundsMax.Y = t.Y; }
		};
		for (std::int32_t i = 0; i < (std::int32_t)routeArc.size(); i++) {
			includeBounds(routeArc[i]);
		}
		for (const auto& m : startMarkers) {
			includeBounds(m);
		}
		boundsMin.X = std::max<std::int32_t>(0, boundsMin.X - 2);
		boundsMin.Y = std::max<std::int32_t>(0, boundsMin.Y - 2);
		boundsMax.X = std::min<std::int32_t>(W - 1, boundsMax.X + 2);
		boundsMax.Y = std::min<std::int32_t>(H - 1, boundsMax.Y + 2);
		outBoundsMin = boundsMin;
		outBoundsMax = boundsMax;

		// Checkpoints = the route's corners (direction changes), so straight runs stay sparse while bends and jump
		// arcs get the detail they need; decimated uniformly if there are too many. The group id is carried so the
		// minimap breaks the polyline at teleports, and a group change at a corner is always kept as a corner.
		SmallVector<Vector2i, 0> corners;
		SmallVector<std::uint8_t, 0> cornerGroup;
		for (std::int32_t i = 0; i < (std::int32_t)routeArc.size(); i++) {
			if (i == 0 || i == (std::int32_t)routeArc.size() - 1) {
				corners.push_back(routeArc[i]);
				cornerGroup.push_back(routeArcGroup[i]);
				continue;
			}
			Vector2i p = routeArc[i - 1], c2 = routeArc[i], n = routeArc[i + 1];
			std::int32_t d1x = (c2.X > p.X) - (c2.X < p.X), d1y = (c2.Y > p.Y) - (c2.Y < p.Y);
			std::int32_t d2x = (n.X > c2.X) - (n.X < c2.X), d2y = (n.Y > c2.Y) - (n.Y < c2.Y);
			if (d1x != d2x || d1y != d2y || routeArcGroup[i] != routeArcGroup[i - 1] || routeArcGroup[i] != routeArcGroup[i + 1]) {
				corners.push_back(routeArc[i]);
				cornerGroup.push_back(routeArcGroup[i]);
			}
		}

		LOGI("Auto-placing minimap track ({}): spawn [{}, {}], far [{}, {}], target [{}, {}], region {} tiles, warpFreeRoute={}, route {}, arc {}, corners {}, bounds [{}, {}]-[{}, {}]",
			routeType == TrackRouteType::Lap ? "lap" : "to exit", spawnTile.X, spawnTile.Y, farTile.X, farTile.Y, target.X, target.Y, regionSize, routeNoWarp ? 1 : 0,
			(std::int32_t)route.size(), (std::int32_t)routeArc.size(), (std::int32_t)corners.size(),
			boundsMin.X, boundsMin.Y, boundsMax.X, boundsMax.Y);

		const std::int32_t cornerCount = (std::int32_t)corners.size();
		constexpr std::int32_t MaxCheckpoints = 100;
		orderedCheckpoints.clear();
		if (cornerCount <= MaxCheckpoints) {
			for (std::int32_t i = 0; i < cornerCount; i++) {
				orderedCheckpoints.push_back({ corners[i], (std::uint16_t)i, cornerGroup[i] });
			}
		} else {
			for (std::int32_t k = 0; k < MaxCheckpoints; k++) {
				std::int32_t idx = (std::int32_t)((std::int64_t)k * (cornerCount - 1) / (MaxCheckpoints - 1));
				orderedCheckpoints.push_back({ corners[idx], (std::uint16_t)k, cornerGroup[idx] });
			}
		}

		if (orderedCheckpoints.size() < 2) {
			orderedCheckpoints.clear();
			LOGW("Cannot auto-place minimap track: could not derive a track from level geometry");
			return;
		}

		// The route is directional (spawn -> finish), so it can be trusted for progress-based ranking
		outCheckpointsOrdered = true;
		LOGI("Auto-placed {} minimap track checkpoints (finish tile [{}, {}])",
			(std::int32_t)orderedCheckpoints.size(), finishTile.X, finishTile.Y);
	}
}

#endif
