#include "RaceRouteGenerator.h"

#if defined(WITH_MULTIPLAYER)

#include "../Tiles/TileMap.h"
#include "../Events/EventMap.h"

#include <queue>

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

	void GenerateRaceRouteFromGeometry(Tiles::TileMap* tileMap, Events::EventMap* eventMap,
		const SmallVector<MultiplayerSpawnPoint, 0>& spawnPoints, const SmallVector<Vector2i, 0>& startMarkers,
		SmallVector<RaceCheckpoint, 0>& orderedCheckpoints, Vector2i& outBoundsMin, Vector2i& outBoundsMax,
		bool& outCheckpointsOrdered)
	{
		// Heuristic auto-placement for original race levels that don't carry JJ2+ waypoint Text events.
		// We trace the actual walkable route the player would take: from a spawn point, out to the farthest
		// reachable point (the far side of the loop), and back to the "Set Lap" warp via the opposite arm of
		// the track. Checkpoints are then sampled evenly along that route, so the minimap reflects the real
		// level geometry and starts at the spawn / ends at the Set Lap warp.
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
			LOGI("Auto-placing race checkpoints: aggregated {} of {} spawn point(s)", clustered, (std::int32_t)spawnPoints.size());
		} else {
			spawnPos = eventMap->GetSpawnPosition(PlayerType::Jazz);
		}
		if (spawnPos.X < 0.0f || spawnPos.Y < 0.0f) {
			LOGW("Cannot auto-place race checkpoints: no valid spawn position");
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
		// Warps that teleport the player to another section (so the route can continue from the destination). The
		// "Set Lap" warp (EventParams[2] != 0) is the lap finish and is handled separately, so it's excluded here.
		// Warp targets live in a separate EventMap list (not the event layout), so they're resolved via GetWarpTarget().
		SmallVector<Pair<Vector2i, std::uint32_t>, 0> warpOrigins;
		// Level-exit events (end-of-level area / sign): the finish when there's no "Set Lap" warp, mirroring how
		// Race mode itself falls back to level exits for lap completion
		SmallVector<Vector2i, 0> exitMarkers;
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
				case EventType::ModifierTube:
					// Tubes transport the player through (often narrow/diagonal) passages regardless of geometry
					tubeMap[x + y * W] = 1;
					break;
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

		// A tile the player can occupy - a single free tile is enough (the player can crawl through 1-tile gaps);
		// tube tiles count too, even if their terrain is solid, since the tube transports the player through them.
		// Lift tiles (vine/pole/hook/float-up) are occupiable even when their tile graphic has a solid mask - the
		// player grabs and climbs them, so they must not read as a ceiling that blocks the upward boost.
		auto occupiable = [&isFree, &isTube, &isLift, W, H](std::int32_t tx, std::int32_t ty) -> bool {
			return (tx >= 0 && ty >= 0 && tx < W && ty < H && (isFree(tx, ty) || isTube(tx, ty) || isLift(tx, ty)));
		};
		// The player stands here if there's a solid tile/level floor, a one-way platform or a bridge/platform below,
		// or a spring at its feet/below (springs are actors on otherwise-empty tiles, but the player rests on them)
		auto hasGround = [&isFree, &isOneWay, &onSurface, &springAt, H](std::int32_t tx, std::int32_t ty) -> bool {
			return (ty + 1 >= H) || !isFree(tx, ty + 1) || isOneWay(tx, ty + 1) || onSurface(tx, ty) || onSurface(tx, ty + 1)
				|| springAt(tx, ty) != 0 || springAt(tx, ty + 1) != 0;
		};
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

		Vector2i spawnTile = findSeed(Vector2i((std::int32_t)(spawnPos.X / Tiles::TileSet::DefaultTileSize), (std::int32_t)(spawnPos.Y / Tiles::TileSet::DefaultTileSize)));
		if (spawnTile.X < 0) {
			LOGW("Cannot auto-place race checkpoints: spawn area is not walkable");
			return;
		}

		// Resolve each warp origin to a standable destination tile (matched by warp ID), so the BFS can teleport.
		// GetWarpTarget() returns the destination in world (pixel) coordinates, or (-1,-1) if the ID is unknown.
		constexpr std::int32_t WarpTS = (std::int32_t)Tiles::TileSet::DefaultTileSize;
		HashMap<Vector2i, Vector2i, TileCoordHash> warpJump;
		for (const auto& origin : warpOrigins) {
			Vector2f targetWorld = eventMap->GetWarpTarget(origin.second());
			if (targetWorld.X >= 0.0f && targetWorld.Y >= 0.0f) {
				Vector2i dest = findSeed(Vector2i((std::int32_t)(targetWorld.X / WarpTS), (std::int32_t)(targetWorld.Y / WarpTS)));
				if (dest.X >= 0) {
					warpJump[origin.first()] = dest;
				}
			}
		}

		// Jump / boost envelope (in tiles)
		constexpr std::int32_t JumpHeight = 7;		// How high the player can jump straight up
		constexpr std::int32_t JumpReachX = 10;		// Horizontal reach of a (running) jump - clears fairly wide gaps
		constexpr std::int32_t JumpDropY = 6;		// How far below the player can land when jumping across a gap
		constexpr std::int32_t BoostHeight = 64;	// Max tiles a spring/float-up can carry the player up a clear column
		constexpr std::int32_t BoostReachX = 3;		// Sideways reach when stepping off a vertical boost
		constexpr std::int32_t HSpringReachX = 16;	// Horizontal reach of a running jump off a spring (clears wide pits)
		constexpr std::int32_t PoleReachY = 8;		// How far a spinning pole carries the player to the next pole (kept modest so it doesn't vault whole sections)

		// Finds a clear arc for a jump from (sx,sy) to (sx+dx,sy+dy) - rise in the start column, travel across at
		// the apex, then descend to the landing - without passing through solid tiles, trying higher apexes up to
		// maxUp to clear taller obstacles. Returns the apex row, or INT32_MAX if no clear arc exists.
		auto jumpApex = [&isFree, &isOneWay](std::int32_t sx, std::int32_t sy, std::int32_t dx, std::int32_t dy, std::int32_t maxUp) -> std::int32_t {
			std::int32_t ex = sx + dx, ey = sy + dy;
			std::int32_t x0 = (sx < ex ? sx : ex), x1 = (sx < ex ? ex : sx);
			std::int32_t apexStart = (sy < ey ? sy : ey);
			for (std::int32_t apexY = apexStart; apexY >= sy - maxUp; apexY--) {
				bool ok = true;
				for (std::int32_t yy = sy - 1; yy >= apexY; yy--) {
					if (!isFree(sx, yy)) { ok = false; break; }
				}
				if (!ok) {
					break; // ceiling above the start: a higher apex is impossible too
				}
				for (std::int32_t xx = x0; xx <= x1; xx++) {
					if (!isFree(xx, apexY)) { ok = false; break; }
				}
				if (!ok) {
					continue; // wall at this height; try a higher apex
				}
				// Descending toward the landing: a one-way platform is solid from above, so the arc can't drop
				// down through it (the rise phase above may still pass up through one-way platforms)
				for (std::int32_t yy = apexY + 1; yy <= ey; yy++) {
					if (!isFree(ex, yy) || isOneWay(ex, yy)) { ok = false; break; }
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

		// Gravity-aware breadth-first search recording predecessors, so a route can be reconstructed.
		// 'blocked' optionally forbids tiles (used to force the second pass around the other arm of the loop).
		// 'useWarps' toggles warp teleport edges: enabled for reachability, disabled when tracing a route so it
		// follows the geometry (e.g., climbs poles) rather than teleporting past it.
		auto runBfs = [&](Vector2i start, const std::uint8_t* blocked, std::int32_t* parent, std::int32_t* dist, Vector2i& farTile, std::int32_t& visitedCount, bool useWarps) {
			std::queue<Vector2i> q;
			std::int32_t startIdx = start.X + start.Y * W;
			dist[startIdx] = 0;
			farTile = start;
			std::int32_t farDist = 0;
			visitedCount = 0;
			q.push(start);

			auto tryAdd = [&](std::int32_t tx, std::int32_t ty, std::int32_t fromDist, std::int32_t fromIdx) {
				if (!occupiable(tx, ty)) {
					return;
				}
				std::int32_t ni = tx + ty * W;
				if (dist[ni] >= 0 || (blocked != nullptr && blocked[ni] != 0)) {
					return;
				}
				dist[ni] = fromDist + 1;
				parent[ni] = fromIdx;
				q.push(Vector2i(tx, ty));
			};

			// Adds a tile without the occupiable check (the caller verified it can be entered, e.g., a slope tile
			// reached diagonally through its empty corner)
			auto tryAddRaw = [&](std::int32_t tx, std::int32_t ty, std::int32_t fromDist, std::int32_t fromIdx) {
				if (tx < 0 || ty < 0 || tx >= W || ty >= H) {
					return;
				}
				std::int32_t ni = tx + ty * W;
				if (dist[ni] >= 0 || (blocked != nullptr && blocked[ni] != 0)) {
					return;
				}
				dist[ni] = fromDist + 1;
				parent[ni] = fromIdx;
				q.push(Vector2i(tx, ty));
			};

			while (!q.empty()) {
				Vector2i c = q.front(); q.pop();
				visitedCount++;
				std::int32_t ci = c.X + c.Y * W;
				std::int32_t cd = dist[ci];

				// A node sitting inside a partially-solid tile (a slope or a thin solid band, reached via the diagonal
				// step) rests on that tile's solid part - treat it as grounded so the tracer doesn't fall straight
				// through it (fixes the player dropping through a middle-solid tile after a tube ends). Destructible
				// tiles are excluded: the player breaks through them (e.g., buttstomps down), so they must not read as
				// standable ground - !isFree(c) is true only for genuine solid terrain (slopes/bands), not breakables.
				bool grounded = hasGround(c.X, c.Y) || (tileMap->IsTilePartiallySolid(c.X, c.Y) && !isFree(c.X, c.Y));
				bool lift = isLift(c.X, c.Y);
				bool tube = isTube(c.X, c.Y);
				// A spinning pole flings the player in the direction of entry momentum (see Player::NextPoleStage):
				// up if they rose into it, down if they descended into it (or entered from the side, where gravity
				// dominates). Approximate the entry direction from how the BFS arrived, so a pole on a downward
				// section doesn't launch the route up and over it.
				bool poleHere = isPole(c.X, c.Y);
				bool poleUp = poleHere && (parent[ci] >= 0 && (parent[ci] / W) > c.Y);
				bool poleDown = poleHere && !poleUp;

				// Only consider real footing (not transient air tiles above a spring/jump) as the farthest point
				if (cd > farDist && (grounded || lift || tube)) {
					farDist = cd;
					farTile = c;
				}

				if (tube) {
					// Inside a tube the player is transported freely through the connected passage (any direction,
					// ignoring gravity and tight geometry)
					tryAdd(c.X - 1, c.Y, cd, ci);
					tryAdd(c.X + 1, c.Y, cd, ci);
					tryAdd(c.X, c.Y - 1, cd, ci);
					tryAdd(c.X, c.Y + 1, cd, ci);
				}

				// A warp teleports the player to its destination; continue tracing the route from there
				if (useWarps) {
					auto warp = warpJump.find(c);
					if (warp != warpJump.end()) {
						tryAddRaw(warp->second.X, warp->second.Y, cd, ci);
					}
				}

				std::uint8_t springHere = (springAt(c.X, c.Y) != 0 ? springAt(c.X, c.Y) : springAt(c.X, c.Y + 1));
				// Springs and vines/hooks/float-up always carry upward; a pole only when the player rose into it
				bool boostUp = springHere == 1 || (lift && !poleHere) || poleUp;

				if (boostUp) {
					// Vertical springs launch the player a fixed height (per spring type); float-up/vine/pole areas
					// carry the player up the whole clear column. Ride upward (bounded by that cap or a ceiling) and
					// step off onto any ledge within reach along the way.
					// Poles only carry a modest distance (they don't lift the player the whole shaft like a float-up
					// area or vine), so cap them low; springs use their per-type height, vines/float-up the full column
					std::int32_t cap = poleHere ? PoleReachY : (lift ? BoostHeight : springBoostAt(c.X, c.Y));
					if (cap == 0) {
						cap = springBoostAt(c.X, c.Y + 1);
					}
					if (cap <= 0 || cap > BoostHeight) {
						cap = BoostHeight;
					}
					for (std::int32_t k = 1; k <= cap; k++) {
						std::int32_t ty = c.Y - k;
						if (!occupiable(c.X, ty)) {
							break; // ceiling
						}
						tryAdd(c.X, ty, cd, ci);
						for (std::int32_t dir = -1; dir <= 1; dir += 2) {
							for (std::int32_t s = 1; s <= BoostReachX; s++) {
								std::int32_t sx = c.X + dir * s;
								if (!occupiable(sx, ty)) {
									break; // wall blocks stepping further sideways at this height
								}
								if (hasGround(sx, ty) || isLift(sx, ty)) {
									tryAdd(sx, ty, cd, ci);
								}
							}
						}
					}
				}

				if (poleDown && !isOneWay(c.X, c.Y + 1)) {
					// Flung downward: descend the pole's column (then keeps falling below it via the airborne logic);
					// never drop down through a one-way platform (solid from above)
					tryAdd(c.X, c.Y + 1, cd, ci);
				}

				if (lift) {
					// On a vine/pole the player can also move sideways
					tryAdd(c.X - 1, c.Y, cd, ci);
					tryAdd(c.X + 1, c.Y, cd, ci);
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
								tryAdd(tx, ty, cd, ci);
							}
						}
					}
				}

				if (!grounded && !lift && !tube) {
					// Airborne: fall, drifting horizontally as it descends - but never INTO a one-way platform (those
					// are solid from above; the player may only pass UP through them), and never upward
					if (!isOneWay(c.X, c.Y + 1)) {
						tryAdd(c.X, c.Y + 1, cd, ci);
					}
					if (!isOneWay(c.X - 1, c.Y + 1)) {
						tryAdd(c.X - 1, c.Y + 1, cd, ci);
					}
					if (!isOneWay(c.X + 1, c.Y + 1)) {
						tryAdd(c.X + 1, c.Y + 1, cd, ci);
					}
					if (isFree(c.X - 1, c.Y + 1) && !isOneWay(c.X - 2, c.Y + 1)) {
						tryAdd(c.X - 2, c.Y + 1, cd, ci);
					}
					if (isFree(c.X + 1, c.Y + 1) && !isOneWay(c.X + 2, c.Y + 1)) {
						tryAdd(c.X + 2, c.Y + 1, cd, ci);
					}
				} else if (grounded) {
					// On a bridge/platform surface the player is held up over empty space and must WALK across it -
					// they can't descend off it (down a slope at its end, or by jumping/falling through it). Once on
					// solid ground past the bridge, descending is allowed again.
					bool onBridgeSurface = onSurface(c.X, c.Y) || onSurface(c.X, c.Y + 1);

					// Walk to either side (stepping off a ledge then falls via gravity)
					tryAdd(c.X - 1, c.Y, cd, ci);
					tryAdd(c.X + 1, c.Y, cd, ci);

					// Step one tile diagonally to follow a slope or squeeze through a tight 1-tile diagonal corridor.
					// Only when the straight-sideways tile is blocked (otherwise walk/jump/fall already handle it -
					// and this stops the tracer from dropping diagonally off an open ledge or bridge). 45-degree
					// slope tiles read as "solid" on the grid, so a partially-solid tile is enterable - but ONLY when
					// it's a genuine triangular slope: the corner facing the player is clear AND the diagonally
					// opposite corner is solid. A thin diagonal line or a middle-solid band has both corners clear,
					// so this check refuses to step into (and through) it.
					for (std::int32_t dir = -1; dir <= 1; dir += 2) {
						if (occupiable(c.X + dir, c.Y)) {
							continue;
						}
						std::int32_t cornerX = (dir < 0 ? 1 : -1); // destination corner facing the player
						std::int32_t ux = c.X + dir, uy = c.Y - 1;
						bool upSlope = !occupiable(ux, uy) && tileMap->IsTileCornerEmpty(ux, uy, cornerX, 1)
							&& !tileMap->IsTileCornerEmpty(ux, uy, -cornerX, -1);
						if ((occupiable(ux, uy) && (hasGround(ux, uy) || isLift(ux, uy))) || upSlope) {
							tryAddRaw(ux, uy, cd, ci);
						}
						// Descend diagonally ONLY into a genuine slope tile. Dropping into an open tile past a solid
						// side would clip the solid corner - that's what made the tracer fall off the end of a bridge
						// past its solid anchor instead of stepping up onto it (the diagonal-up branch above handles
						// that). A normal walk + gravity covers stepping down onto a lower ledge with an open side.
						std::int32_t dxt = c.X + dir, dyt = c.Y + 1;
						bool downSlope = !occupiable(dxt, dyt) && tileMap->IsTileCornerEmpty(dxt, dyt, cornerX, -1)
							&& !tileMap->IsTileCornerEmpty(dxt, dyt, -cornerX, 1);
						if (downSlope && !isOneWay(dxt, dyt) && !onBridgeSurface) {
							tryAddRaw(dxt, dyt, cd, ci);
						}
					}

					// Jump onto reachable ledges, surfaces or movement aids within the envelope, only when the arc
					// is clear of solids. When standing on a bridge/platform surface (which holds the player up over
					// empty space), DON'T allow a downward jump: jumpClear sees the empty space below the surface as
					// "clear" and would otherwise let the tracer drop straight down THROUGH the bridge it's crossing.
					// The player must walk across; once on solid ground past it they can descend normally.
					std::int32_t maxDrop = (onBridgeSurface ? 0 : JumpDropY);
					for (std::int32_t dx = -JumpReachX; dx <= JumpReachX; dx++) {
						std::int32_t adx = (dx < 0 ? -dx : dx);
						std::int32_t up = JumpHeight - adx;
						if (up < 0) {
							up = 0;
						}
						for (std::int32_t dy = -up; dy <= maxDrop; dy++) {
							if (dx == 0 && dy == 0) {
								continue;
							}
							std::int32_t tx = c.X + dx, ty = c.Y + dy;
							if ((hasGround(tx, ty) || isLift(tx, ty)) && occupiable(tx, ty) && jumpClear(c.X, c.Y, dx, dy, JumpHeight)) {
								tryAdd(tx, ty, cd, ci);
							}
						}
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
		runBfs(spawnTile, nullptr, parent.get(), dist.get(), farTile, regionSize, true);

		if (regionSize < 32) {
			LOGW("Cannot auto-place race checkpoints: walkable region is too small ({} tiles)", regionSize);
			return;
		}
		if (regionSize > (totalTiles * 3) / 5) {
			LOGW("Cannot auto-place race checkpoints: level looks like an open arena, not a track");
			return;
		}

		const std::int32_t spawnIdx = spawnTile.X + spawnTile.Y * W;
		std::int32_t farIdx = farTile.X + farTile.Y * W;

		// The finish is the "Set Lap" warp, or - if the level has none - a level-exit event (end-of-level area or
		// EOL sign), mirroring how Race mode itself falls back to level exits for lap completion. A full lap goes
		// spawn -> (out to the far side) -> finish; routing via the far tile forces the trace around the whole loop
		// even when the start line sits right next to the finish (so there's no short path to block). Among the
		// candidates, take the one reachable from spawn and farthest along the track, so the route spans the level.
		auto pickFarthestReachable = [&](const SmallVector<Vector2i, 0>& markers) -> Vector2i {
			Vector2i best(-1, -1);
			std::int32_t bestDist = -1;
			for (const auto& m : markers) {
				Vector2i t = findSeed(m);
				if (t.X >= 0) {
					std::int32_t d = dist[t.X + t.Y * W];
					if (d > bestDist) { bestDist = d; best = t; }
				}
			}
			return best;
		};
		Vector2i finishTile = pickFarthestReachable(startMarkers);
		const char* finishSource = "warp";
		if (finishTile.X < 0) {
			finishTile = pickFarthestReachable(exitMarkers);
			finishSource = (finishTile.X >= 0 ? "exit" : "far");
		}
		Vector2i target = farTile;
		if (finishTile.X >= 0 && dist[finishTile.X + finishTile.Y * W] >= 0) {
			target = finishTile;
			// If the finish itself lies far from the spawn (a point-to-point race, not a loop back to the start),
			// make it the turnaround too, so the route runs straight to it instead of overshooting to the farthest
			// tile and doubling back past the finish.
			std::int32_t finDist = dist[finishTile.X + finishTile.Y * W];
			if (dist[farIdx] > 0 && finDist * 2 >= dist[farIdx]) {
				farTile = finishTile;
				farIdx = finishTile.X + finishTile.Y * W;
			}
		}
		const std::int32_t targetIdx = target.X + target.Y * W;

		std::int32_t springUp = 0, springSide = 0, liftCount = 0, surfaceCount = 0, poleCount = 0;
		std::int32_t springUpReached = 0, liftReached = 0, poleReached = 0;
		for (std::int32_t i = 0; i < totalTiles; i++) {
			if (springMap[i] == 1) { springUp++; if (dist[i] >= 0) { springUpReached++; } }
			else if (springMap[i] == 2) { springSide++; }
			if (liftMap[i] != 0) { liftCount++; if (dist[i] >= 0) { liftReached++; } }
			if (liftMap[i] == 2) { poleCount++; if (dist[i] >= 0) { poleReached++; } }
			if (surfaceMap[i] != 0) { surfaceCount++; }
		}
		LOGI("Race geometry: {} vertical springs ({} reached) + {} horizontal, {} lift ({} reached, of which {} poles {} reached), {} surface, {} warp(s), {} exit(s); finish [{}, {}] via {} reachable={} (dist {})",
			springUp, springUpReached, springSide, liftCount, liftReached, poleCount, poleReached, surfaceCount, (std::int32_t)warpJump.size(), (std::int32_t)exitMarkers.size(),
			finishTile.X, finishTile.Y, finishSource,
			(finishTile.X >= 0 && dist[finishTile.X + finishTile.Y * W] >= 0) ? 1 : 0,
			(finishTile.X >= 0 ? dist[finishTile.X + finishTile.Y * W] : -1));
		// Each resolved warp and whether the BFS actually reached its origin (so it could teleport) - helps diagnose
		// warps the tracer stops at instead of following
		for (const auto& wj : warpJump) {
			std::int32_t oi = wj.first.X + wj.first.Y * W;
			LOGI("Race warp: origin [{}, {}] (reached={}) -> dest [{}, {}] (reached={})",
				wj.first.X, wj.first.Y, (dist[oi] >= 0) ? 1 : 0,
				wj.second.X, wj.second.Y, (dist[wj.second.X + wj.second.Y * W] >= 0) ? 1 : 0);
		}

		// First arm: spawn -> far. Prefer a warp-free route so the line follows the geometry (e.g., climbs the poles
		// or vines) instead of teleporting past it via a warp shortcut; fall back to the warp-enabled route only
		// when the far tile can't be reached without warps (e.g., a section only accessible by a warp).
		std::unique_ptr<std::int32_t[]> parentNW = std::make_unique<std::int32_t[]>((std::size_t)totalTiles);
		std::unique_ptr<std::int32_t[]> distNW = std::make_unique<std::int32_t[]>((std::size_t)totalTiles);
		for (std::int32_t i = 0; i < totalTiles; i++) { parentNW[i] = -1; distNW[i] = -1; }
		Vector2i farNW;
		std::int32_t regionNW = 0;
		runBfs(spawnTile, nullptr, parentNW.get(), distNW.get(), farNW, regionNW, false);
		bool routeNoWarp = (distNW[farIdx] >= 0);

		SmallVector<Vector2i, 0> pathOut;
		reconstruct(routeNoWarp ? parentNW.get() : parent.get(), spawnIdx, farIdx, pathOut);

		// Second arm: far -> finish, taking the opposite side by blocking the first arm. If blocking disconnects
		// the finish (e.g., wide corridors), retry without blocking so the route still reaches the end.
		std::unique_ptr<std::uint8_t[]> blocked = std::make_unique<std::uint8_t[]>((std::size_t)totalTiles);
		for (std::int32_t i = 1; i + 1 < (std::int32_t)pathOut.size(); i++) {
			blocked[pathOut[i].X + pathOut[i].Y * W] = 1;
		}

		std::unique_ptr<std::int32_t[]> parent2 = std::make_unique<std::int32_t[]>((std::size_t)totalTiles);
		std::unique_ptr<std::int32_t[]> dist2 = std::make_unique<std::int32_t[]>((std::size_t)totalTiles);
		for (std::int32_t i = 0; i < totalTiles; i++) { parent2[i] = -1; dist2[i] = -1; }

		Vector2i far2;
		std::int32_t visited2 = 0;
		runBfs(farTile, blocked.get(), parent2.get(), dist2.get(), far2, visited2, !routeNoWarp);

		if (dist2[targetIdx] < 0) {
			// Blocking the first arm cut off the finish; retry unblocked so we still reach it
			for (std::int32_t i = 0; i < totalTiles; i++) { parent2[i] = -1; dist2[i] = -1; }
			runBfs(farTile, nullptr, parent2.get(), dist2.get(), far2, visited2, !routeNoWarp);
		}
		if (dist2[targetIdx] < 0 && routeNoWarp) {
			// Still unreachable without warps - allow warps for the return arm so the route completes
			for (std::int32_t i = 0; i < totalTiles; i++) { parent2[i] = -1; dist2[i] = -1; }
			runBfs(farTile, nullptr, parent2.get(), dist2.get(), far2, visited2, true);
		}

		SmallVector<Vector2i, 0> pathBack;
		if (dist2[targetIdx] >= 0) {
			reconstruct(parent2.get(), farIdx, targetIdx, pathBack);
		}

		// Assemble the route spawn -> far -> finish. But if the far tile overshoots just past the finish (e.g., a
		// catch-spring sits a couple of tiles beyond the finish warp), end the route at the finish instead of
		// looping out to far and back.
		SmallVector<Vector2i, 0> route;
		std::int32_t trimAt = -1;
		if (target.X == finishTile.X && target.Y == finishTile.Y) {
			for (std::int32_t i = (std::int32_t)pathOut.size() / 2; i < (std::int32_t)pathOut.size(); i++) {
				std::int32_t ddx = pathOut[i].X - finishTile.X, ddy = pathOut[i].Y - finishTile.Y;
				if (std::max(ddx < 0 ? -ddx : ddx, ddy < 0 ? -ddy : ddy) <= 2) {
					trimAt = i;
					break;
				}
			}
		}
		if (trimAt >= 0) {
			for (std::int32_t i = 0; i <= trimAt; i++) {
				route.push_back(pathOut[i]);
			}
			Vector2i last = route[route.size() - 1];
			if (last.X != finishTile.X || last.Y != finishTile.Y) {
				route.push_back(finishTile);
			}
		} else {
			for (std::int32_t i = 0; i < (std::int32_t)pathOut.size(); i++) {
				route.push_back(pathOut[i]);
			}
			for (std::int32_t i = 1; i < (std::int32_t)pathBack.size(); i++) {
				route.push_back(pathBack[i]);
			}
		}

		if (route.size() < 2) {
			LOGW("Cannot auto-place race checkpoints: could not trace a route through the level");
			return;
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
		boundsMin.X = std::max(0, boundsMin.X - 2);
		boundsMin.Y = std::max(0, boundsMin.Y - 2);
		boundsMax.X = std::min(W - 1, boundsMax.X + 2);
		boundsMax.Y = std::min(H - 1, boundsMax.Y + 2);
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

		LOGI("Auto-placing race checkpoints: spawn [{}, {}], far [{}, {}], target [{}, {}], region {} tiles, warpFreeRoute={}, route {}, arc {}, corners {}, bounds [{}, {}]-[{}, {}]",
			spawnTile.X, spawnTile.Y, farTile.X, farTile.Y, target.X, target.Y, regionSize, routeNoWarp ? 1 : 0,
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
			LOGW("Cannot auto-place race checkpoints: could not derive a track from level geometry");
			return;
		}

		// The route is directional (spawn -> finish), so it can be trusted for progress-based ranking
		outCheckpointsOrdered = true;
		LOGI("Auto-placed {} race checkpoints (finish tile [{}, {}])",
			(std::int32_t)orderedCheckpoints.size(), finishTile.X, finishTile.Y);
	}
}

#endif
