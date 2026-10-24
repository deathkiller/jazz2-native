#include "EventMap.h"
#include "../Formats/LevelFile.h"
#include "../Tiles/TileMap.h"
#include "../WeatherType.h"

#include "../../nCine/tracy.h"
#include "../../nCine/Base/Random.h"
#include "../../nCine/Base/FrameTimer.h"

namespace Jazz2::Events
{
	EventMap::EventMap(Vector2i layoutSize)
		: _levelHandler(nullptr), _layoutSize(layoutSize), _pitType(PitType::FallForever), _hasRollbackCheckpoint(false), _layoutRows(layoutSize.Y),
			_spawningTile(-1, -1), _spawningPos(0, 0), _spawningType(EventType::Empty)
	{
	}

	std::int32_t EventMap::GetLayoutCount() const
	{
		return _layoutSize.X * _layoutRows;
	}

	bool EventMap::IsInLayout(std::int32_t x, std::int32_t y) const
	{
		if (x < 0 || y < 0 || x >= _layoutSize.X || y >= _layoutRows) {
			return false;
		}
		// The last row of the off-grid events can be partly unused
		return (y < _layoutSize.Y || (x + (y - _layoutSize.Y) * _layoutSize.X) < (std::int32_t)_offGridPositions.size());
	}

	Vector2i EventMap::GetEventPosition(std::int32_t x, std::int32_t y) const
	{
		if (y >= _layoutSize.Y && IsInLayout(x, y)) {
			return _offGridPositions[x + (y - _layoutSize.Y) * _layoutSize.X];
		}
		return Vector2i(x * Tiles::TileSet::DefaultTileSize + Tiles::TileSet::DefaultTileSize / 2,
			y * Tiles::TileSet::DefaultTileSize + Tiles::TileSet::DefaultTileSize / 2);
	}

	Vector2i EventMap::GetOriginTile(Vector2i pos, EventType type)
	{
		// The tile whose object is being spawned right now is known exactly, a grid event and an off-grid one of the
		// same type can share a position (the center of a tile). The object asks first, before its own activation
		// runs, so whatever it spawns there while activating mustn't get the tile too (it would reset its event).
		if (_spawningTile.X >= 0 && _spawningPos == pos && _spawningType == type) {
			Vector2i tile = _spawningTile;
			_spawningTile = Vector2i(-1, -1);
			return tile;
		}

		// Otherwise by the position and the type, among the off-grid events in the tile of the position only
		if (pos.X >= 0 && pos.Y >= 0) {
			const std::int32_t column = pos.X / Tiles::TileSet::DefaultTileSize;
			const std::int32_t gridCount = _layoutSize.X * _layoutSize.Y;
			auto range = FindOffGridEvents(pos.Y / Tiles::TileSet::DefaultTileSize, column);
			for (const std::uint16_t* it = range.first; it != range.second; ++it) {
				const std::int32_t index = *it;
				if (_offGridPositions[index].X / Tiles::TileSet::DefaultTileSize != column) {
					break;
				}
				if (_offGridPositions[index] != pos) {
					continue;
				}
				const EventTile& tile = _eventLayout[gridCount + index];
				bool matches = (tile.Event == type);
				if (!matches && tile.Event == EventType::Generator) {
					std::uint32_t generatorIdx = Actors::EventParamsReader(tile.EventParams).GetUint32(0);
					matches = (generatorIdx < _generators.size() && _generators[generatorIdx].Event == type);
				}
				if (matches) {
					return Vector2i(index % _layoutSize.X, _layoutSize.Y + index / _layoutSize.X);
				}
			}
		}
		return Vector2i(pos.X / Tiles::TileSet::DefaultTileSize, pos.Y / Tiles::TileSet::DefaultTileSize);
	}

	std::shared_ptr<Actors::ActorBase> EventMap::SpawnFromTile(EventType type, std::uint8_t* params, Actors::ActorState flags, Vector2i tile, Vector2i pos, std::int32_t z)
	{
		// Kept for GetOriginTile(), which the object asks while it's being activated; restored afterwards, an object
		// can spawn others while it's being activated
		Vector2i prevTile = _spawningTile, prevPos = _spawningPos;
		EventType prevType = _spawningType;
		_spawningTile = tile;
		_spawningPos = pos;
		_spawningType = type;

		std::shared_ptr<Actors::ActorBase> actor = _levelHandler->EventSpawner()->SpawnEvent(type, params, flags, Vector3i(pos.X, pos.Y, z));

		_spawningTile = prevTile;
		_spawningPos = prevPos;
		_spawningType = prevType;
		return actor;
	}

	void EventMap::SpawnTileEvent(EventTile& tile, std::int32_t x, std::int32_t y, Actors::ActorState flags, std::int32_t z)
	{
		tile.IsEventActive = true;

		if (tile.Event == EventType::AreaWeather) {
			_levelHandler->SetWeather((WeatherType)tile.EventParams[0], tile.EventParams[1]);
		} else if (tile.Event != EventType::Generator) {
			Vector2i pos = GetEventPosition(x, y);
			std::shared_ptr<Actors::ActorBase> actor = SpawnFromTile(tile.Event, tile.EventParams,
				Actors::ActorState::IsCreatedFromEventMap | tile.EventFlags | flags, Vector2i(x, y), pos, z);
			if (actor != nullptr) {
				_levelHandler->AddActor(actor);
			}
		}
	}

	void EventMap::SetLevelHandler(ILevelHandler* levelHandler)
	{
		_levelHandler = levelHandler;
	}

	Vector2i EventMap::GetSize() const
	{
		return _layoutSize;
	}

	PitType EventMap::GetPitType() const
	{
		return _pitType;
	}

	void EventMap::SetPitType(PitType value)
	{
		_pitType = value;
	}

	Vector2f EventMap::GetSpawnPosition(PlayerType type)
	{
		std::int32_t targetCount = 0;
		for (auto& target : _spawnPoints) {
			if ((target.PlayerTypeMask & (1 << ((std::int32_t)type - 1))) != 0) {
				targetCount++;
			}
		}

		if (targetCount > 0) {
			std::int32_t selectedTarget = nCine::Random().Next(0, targetCount);
			for (auto& target : _spawnPoints) {
				if ((target.PlayerTypeMask & (1 << ((std::int32_t)type - 1))) == 0) {
					continue;
				}
				if (selectedTarget == 0) {
					return target.Pos;
				}
				selectedTarget--;
			}
		}

		// A level made for multiplayer only (the original battle and race levels, and most custom ones of
		// that kind) has no single-player start at all, only multiplayer ones. Those are not collected
		// above - the multiplayer handler picks from them by team - but they are the right place to put a
		// player who starts such a level alone.
		std::int32_t multiplayerTargetCount = 0;
		ForEachEvent([&multiplayerTargetCount](EventTile& e, std::int32_t x, std::int32_t y) {
			if (e.Event == EventType::LevelStartMultiplayer) {
				multiplayerTargetCount++;
			}
			return true;
		});
		if (multiplayerTargetCount > 0) {
			std::int32_t selectedTarget = nCine::Random().Next(0, multiplayerTargetCount);
			Vector2f result(-1, -1);
			ForEachEvent([&selectedTarget, &result](EventTile& e, std::int32_t x, std::int32_t y) {
				if (e.Event != EventType::LevelStartMultiplayer) {
					return true;
				}
				if (selectedTarget == 0) {
					// Same placement as the multiplayer handler gives these markers
					result = Vector2f((float)x * Tiles::TileSet::DefaultTileSize, (float)y * Tiles::TileSet::DefaultTileSize - 8.0f);
					return false;
				}
				selectedTarget--;
				return true;
			});
			return result;
		}

		return Vector2f(-1, -1);
	}

	void EventMap::SaveTileForRollback(std::uint32_t tileIndex, const EventTile& tile)
	{
		// Binary search keeps the list sorted by index and doubles as the duplicate check: only the first
		// saved value of a tile is its checkpoint value, later overwrites must not replace it
		std::size_t lo = 0, hi = _eventLayoutForRollback.size();
		while (lo < hi) {
			std::size_t mid = lo + (hi - lo) / 2;
			if (_eventLayoutForRollback[mid].TileIndex < tileIndex) {
				lo = mid + 1;
			} else {
				hi = mid;
			}
		}

		if (lo < _eventLayoutForRollback.size() && _eventLayoutForRollback[lo].TileIndex == tileIndex) {
			return;
		}

		_eventLayoutForRollback.insert(_eventLayoutForRollback.begin() + lo, RollbackTile { tileIndex, tile });
	}

	void EventMap::CreateCheckpointForRollback()
	{
		std::int32_t layoutSize = GetLayoutCount();

		// Everything but IsEventActive stays as it is until StoreTileEvent() consumes a tile, which saves the
		// previous value itself, so the checkpoint starts out as just the active bits (see RollbackTile)
		if (_eventActiveForRollback.size() != (std::size_t)layoutSize) {
			_eventActiveForRollback.resize(ValueInit, layoutSize);
		}
		for (std::int32_t i = 0; i < layoutSize; i++) {
			_eventActiveForRollback.set(i, _eventLayout[i].IsEventActive);
		}

		_eventLayoutForRollback.clear();
		_hasRollbackCheckpoint = true;
	}

	void EventMap::RollbackToCheckpoint(bool respawnActors)
	{
		if (!_hasRollbackCheckpoint) {
			return;
		}

		// Moved aside for the walk below, which spawns actors and so runs game code that could consume an event
		// tile and insert into the very list being iterated
		SmallVector<RollbackTile, 0> saved = Death::move(_eventLayoutForRollback);

		// The saved tiles are merged into this walk of the grid rather than restored up front, so that an actor
		// spawned below observes exactly the same partially rolled back grid as it did with the full copy
		std::size_t nextSaved = 0;
		const std::int32_t layoutCount = GetLayoutCount();
		for (std::int32_t y = 0; y < _layoutRows; y++) {
			for (std::int32_t x = 0; x < _layoutSize.X; x++) {
				std::int32_t tileID = y * _layoutSize.X + x;
				if (tileID >= layoutCount) {
					break;
				}
				const bool hasSaved = (nextSaved < saved.size() && saved[nextSaved].TileIndex == (std::uint32_t)tileID);
				EventTile* tilePtr = _eventLayout.Find(tileID);
				if (tilePtr == nullptr) {
					if (!hasSaved) {
						continue;		// An empty cell was never active either
					}
					tilePtr = &_eventLayout.Edit(tileID);
				}
				EventTile& tile = *tilePtr;

				bool wasEventActive = _eventActiveForRollback[tileID];
				bool respawn = (wasEventActive && !tile.IsEventActive);

				// Rollback tile
				if (hasSaved) {
					const EventTile& tilePrev = saved[nextSaved].Tile;
					tile.Event = tilePrev.Event;
					tile.EventFlags = tilePrev.EventFlags;
					std::memcpy(tile.EventParams, tilePrev.EventParams, sizeof(tile.EventParams));
					nextSaved++;
				}
				tile.IsEventActive = wasEventActive;

				if (respawnActors && respawn && tile.Event != EventType::Empty) {
					SpawnTileEvent(tile, x, y, Actors::ActorState::None, ILevelHandler::MainPlaneZ);
				}
			}
		}

		// The checkpoint stays valid, it can be rolled back to again, so the list goes back where it was. Its own
		// values win over anything the walk consumed, whose tile was restored right afterwards.
		SmallVector<RollbackTile, 0> consumedWhileRollingBack = Death::move(_eventLayoutForRollback);
		_eventLayoutForRollback = Death::move(saved);
		for (const auto& entry : consumedWhileRollingBack) {
			SaveTileForRollback(entry.TileIndex, entry.Tile);
		}

		// Reset cooldown of all generators
		// TODO: Save also cooldown of all generators to be able to restore them
		for (auto& generator : _generators) {
			generator.TimeLeft = 0.0f;
			if (!respawnActors) {
				// All objects are destroyed and the restored ones are linked again by AttachGeneratorActor()
				generator.SpawnedActor = nullptr;
			}
		}
	}

	void EventMap::StoreTileEvent(std::int32_t x, std::int32_t y, EventType eventType, Actors::ActorState eventFlags, std::uint8_t* tileParams)
	{
		if (!IsInLayout(x, y)) {
			return;
		}

		std::int32_t tileIndex = x + y * _layoutSize.X;
		EventTile& previousEvent = _eventLayout.Edit(tileIndex);

		// The checkpoint doesn't hold a copy of the grid, so the value about to be lost is saved to it here
		if (_hasRollbackCheckpoint) {
			SaveTileForRollback((std::uint32_t)tileIndex, previousEvent);
		}

		EventTile newEvent = {};
		newEvent.Event = eventType,
		newEvent.EventFlags = eventFlags,
		newEvent.IsEventActive = (previousEvent.Event == eventType && previousEvent.IsEventActive);

		// Store event parameters
		if (tileParams != nullptr) {
			std::memcpy(newEvent.EventParams, tileParams, sizeof(newEvent.EventParams));
		}

		previousEvent = newEvent;
	}

	void EventMap::PreloadEventsAsync()
	{
		ZoneScopedC(0x9D5BA3);

		auto eventSpawner = _levelHandler->EventSpawner();

		// TODO
		//ContentResolver::Get().SuspendAsync();

		// Preload all events
		for (std::int32_t i = 0; i < GetLayoutCount(); i++) {
			// TODO: Exclude also some modifiers here ?
			auto& tile = _eventLayout[i];
			if (tile.Event != EventType::Empty && tile.Event != EventType::Generator && tile.Event != EventType::AreaWeather) {
				eventSpawner->PreloadEvent(tile.Event, const_cast<std::uint8_t*>(tile.EventParams));	// Read-only there
			}
		}

		// Preload all generators
		for (auto& generator : _generators) {
			eventSpawner->PreloadEvent(generator.Event, generator.EventParams);
		}

		// Don't wait for finalization of resources, it will be done in a few next frames
		//ContentResolver::Get().ResumeAsync();
	}

	void EventMap::ProcessGenerators(float timeMult)
	{
		ZoneScopedC(0x9D5BA3);

		for (auto& generator : _generators) {
			if (!_eventLayout[generator.EventPos].IsEventActive) {
				// Generator is inactive (and recharging)
				generator.TimeLeft -= timeMult;
			} else if (generator.SpawnedActor == nullptr || generator.SpawnedActor->GetHealth() <= 0) {
				if (generator.TimeLeft <= 0.0f) {
					// Generator is active and is ready to spawn new actor
					generator.TimeLeft = generator.Delay * FrameTimer::FramesPerSecond;

					std::int32_t x = generator.EventPos % _layoutSize.X;
					std::int32_t y = generator.EventPos / _layoutSize.X;
					Vector2i pos = GetEventPosition(x, y);

					generator.SpawnedActor = SpawnFromTile(generator.Event, generator.EventParams, Actors::ActorState::IsFromGenerator,
						Vector2i(x, y), pos, ILevelHandler::SpritePlaneZ);
					if (generator.SpawnedActor != nullptr) {
						_levelHandler->AddActor(generator.SpawnedActor);
					}
				} else {
					// Generator is active and recharging
					generator.TimeLeft -= timeMult;
					if (generator.SpawnedActor != nullptr) {
						generator.SpawnedActor = nullptr;
					}
				}
			}
		}
	}

	void EventMap::ActivateEvents(std::int32_t tx1, std::int32_t ty1, std::int32_t tx2, std::int32_t ty2, bool allowAsync)
	{
		ZoneScopedC(0x9D5BA3);

		std::int32_t x1 = std::max<std::int32_t>(0, tx1);
		std::int32_t x2 = std::min<std::int32_t>(_layoutSize.X - 1, tx2);
		std::int32_t y1 = std::max<std::int32_t>(0, ty1);
		std::int32_t y2 = std::min<std::int32_t>(_layoutSize.Y - 1, ty2);

		// Row-major, matching the layout: the index is `x + y * width`, so y has to be the OUTER loop or the
		// inner step jumps a whole row. It used to be the other way round, which made this walk column-major
		// over a row-major array - every one of the 53x53 = 2809 tiles of an activation zone landed on its
		// own cache line, 6 KB apart at a typical level width, and that was the whole cost of the frame's
		// OnBeginFrame on the consoles (about 1 ms of a 16 ms budget on the PSP). Swapping the two reads the
		// same tiles in the same zone; only the order events activate in changes, and no event depends on it.
		for (std::int32_t y = y1; y <= y2; y++) {
			for (std::int32_t x = x1; x <= x2; x++) {
				EventTile* tilePtr = _eventLayout.Find(x + y * _layoutSize.X);
				if (tilePtr == nullptr) {
					continue;
				}
				EventTile& tile = *tilePtr;
				if (!tile.IsEventActive && tile.Event != EventType::Empty) {
					SpawnTileEvent(tile, x, y, allowAsync ? Actors::ActorState::Async : Actors::ActorState::None, ILevelHandler::SpritePlaneZ);
				}
			}
		}

		// Off-grid events by the tile their position is in, the zone isn't limited to the grid for them, they can lie
		// outside. Only the rows of the zone are visited, and only the part of each row from its first column on, so
		// the cost follows the events around the zone rather than all of them in the level.
		if (!_offGridOrder.empty()) {
			const std::int32_t gridCount = _layoutSize.X * _layoutSize.Y;
			const std::int32_t firstRow = std::max<std::int32_t>(ty1, 0);
			const std::int32_t lastRow = std::min<std::int32_t>(ty2, std::int32_t(_offGridRowStart.size()) - 2);
			for (std::int32_t row = firstRow; row <= lastRow; row++) {
				auto range = FindOffGridEvents(row, tx1);
				for (const std::uint16_t* it = range.first; it != range.second; ++it) {
					const std::int32_t index = *it;
					if (_offGridPositions[index].X / Tiles::TileSet::DefaultTileSize > tx2) {
						break;
					}
					EventTile* tilePtr = _eventLayout.Find(gridCount + index);
					if (tilePtr != nullptr && !tilePtr->IsEventActive && tilePtr->Event != EventType::Empty) {
						SpawnTileEvent(*tilePtr, index % _layoutSize.X, _layoutSize.Y + index / _layoutSize.X,
							allowAsync ? Actors::ActorState::Async : Actors::ActorState::None, ILevelHandler::SpritePlaneZ);
					}
				}
			}
		}
	}

	std::pair<const std::uint16_t*, const std::uint16_t*> EventMap::FindOffGridEvents(std::int32_t row, std::int32_t firstColumn) const
	{
		if (row < 0 || row >= std::int32_t(_offGridRowStart.size()) - 1) {
			return { nullptr, nullptr };
		}
		const std::uint16_t* begin = _offGridOrder.data() + _offGridRowStart[row];
		const std::uint16_t* end = _offGridOrder.data() + _offGridRowStart[row + 1];
		// The row is ordered by the column, so the first event of the column is found by halving
		begin = std::lower_bound(begin, end, firstColumn, [this](std::uint16_t index, std::int32_t column) {
			return (_offGridPositions[index].X / Tiles::TileSet::DefaultTileSize < column);
		});
		return { begin, end };
	}

	void EventMap::BuildOffGridIndex()
	{
		_offGridOrder.clear();
		_offGridRowStart.clear();
		if (_offGridPositions.empty()) {
			return;
		}

		// The positions were limited to the level and a margin around it when they were read, so none is negative and
		// the rows are few
		std::int32_t rowCount = 0;
		for (const Vector2i& pos : _offGridPositions) {
			rowCount = std::max<std::int32_t>(rowCount, pos.Y / Tiles::TileSet::DefaultTileSize + 1);
		}

		// A reader refuses a level with more off-grid events than the 16-bit indices hold, they're counted there before
		// any is left out
		static_assert(Formats::LevelFile::MaxOffGridEvents <= std::uint32_t(UINT16_MAX) + 1, "Off-grid event indices are 16-bit");
		
		_offGridOrder.resize_for_overwrite(_offGridPositions.size());
		for (std::size_t i = 0; i < _offGridPositions.size(); i++) {
			_offGridOrder[i] = std::uint16_t(i);
		}
		std::sort(_offGridOrder.begin(), _offGridOrder.end(), [this](std::uint16_t a, std::uint16_t b) {
			const std::int32_t rowA = _offGridPositions[a].Y / Tiles::TileSet::DefaultTileSize;
			const std::int32_t rowB = _offGridPositions[b].Y / Tiles::TileSet::DefaultTileSize;
			if (rowA != rowB) {
				return (rowA < rowB);
			}
			const std::int32_t columnA = _offGridPositions[a].X / Tiles::TileSet::DefaultTileSize;
			const std::int32_t columnB = _offGridPositions[b].X / Tiles::TileSet::DefaultTileSize;
			if (columnA != columnB) {
				return (columnA < columnB);
			}
			return (a < b);
		});

		_offGridRowStart.resize_for_overwrite(std::size_t(rowCount) + 1);
		std::size_t next = 0;
		for (std::int32_t row = 0; row <= rowCount; row++) {
			while (next < _offGridOrder.size() && _offGridPositions[_offGridOrder[next]].Y / Tiles::TileSet::DefaultTileSize < row) {
				next++;
			}
			_offGridRowStart[row] = std::int32_t(next);
		}
	}

	void EventMap::Deactivate(std::int32_t x, std::int32_t y)
	{
		if (IsInLayout(x, y)) {
			if (EventTile* tile = _eventLayout.Find(x + y * _layoutSize.X)) {
				if (tile->Event != EventType::Empty) {
					tile->IsEventActive = false;
				}
			}
		}
	}

	void EventMap::ResetGenerator(std::int32_t tx, std::int32_t ty)
	{
		if (!IsInLayout(tx, ty)) {
			return;
		}

		// Linked actor was deactivated, but not destroyed
		// Reset its generator, so it can be respawned immediately
		std::uint32_t generatorIdx = Actors::EventParamsReader(_eventLayout[tx + ty * _layoutSize.X].EventParams).GetUint32(0);
		if (generatorIdx >= _generators.size()) {
			// Do nothing if generator if wrongly configured
			return;
		}

		_generators[generatorIdx].TimeLeft = 0.0f;
		_generators[generatorIdx].SpawnedActor = nullptr;
	}

	void EventMap::Activate(std::int32_t x, std::int32_t y)
	{
		if (IsInLayout(x, y)) {
			if (EventTile* tile = _eventLayout.Find(x + y * _layoutSize.X)) {
				if (tile->Event != EventType::Empty) {
					tile->IsEventActive = true;
				}
			}
		}
	}

	void EventMap::AttachGeneratorActor(std::int32_t tx, std::int32_t ty, std::shared_ptr<Actors::ActorBase> actor)
	{
		if (!IsInLayout(tx, ty)) {
			return;
		}

		EventTile* tile = _eventLayout.Find(tx + ty * _layoutSize.X);
		if (tile == nullptr || tile->Event != EventType::Generator) {
			return;
		}

		std::uint32_t generatorIdx = Actors::EventParamsReader(tile->EventParams).GetUint32(0);
		if (generatorIdx >= _generators.size()) {
			return;
		}

		tile->IsEventActive = true;
		_generators[generatorIdx].SpawnedActor = Death::move(actor);
	}

	const EventMap::EventTile& EventMap::GetEventTile(std::int32_t x, std::int32_t y) const
	{
		static const EventTile Empty = {};
		return (IsInLayout(x, y) ? _eventLayout[x + y * _layoutSize.X] : Empty);
	}

	bool EventMap::GetGeneratorEvent(std::int32_t x, std::int32_t y, EventType& type, const std::uint8_t*& params, std::uint8_t& delay) const
	{
		const EventTile& tile = GetEventTile(x, y);
		if (tile.Event != EventType::Generator) {
			return false;
		}

		std::uint32_t generatorIdx = Actors::EventParamsReader(tile.EventParams).GetUint32(0);
		if (generatorIdx >= _generators.size()) {
			return false;
		}

		const GeneratorInfo& generator = _generators[generatorIdx];
		type = generator.Event;
		params = generator.EventParams;
		delay = generator.Delay;
		return true;
	}

	EventType EventMap::GetStartOrWarpTarget(std::int32_t x, std::int32_t y, std::uint8_t* params) const
	{
		// Where AddWarpTarget() and AddSpawnPosition() put them from the tile
		constexpr float TileSize = float(Tiles::TileSet::DefaultTileSize);
		std::memset(params, 0, Formats::LevelFile::EventParamsSize);
		for (const WarpTarget& target : _warpTargets) {
			if (std::int32_t(std::floor(target.Pos.X / TileSize)) == x && std::int32_t(std::floor(target.Pos.Y / TileSize)) == y) {
				params[0] = std::uint8_t(target.Id);
				params[1] = std::uint8_t(target.Id >> 8);
				std::memcpy(params + Formats::EventRecord::OriginalParamsOffset, target.KeptParams, sizeof(target.KeptParams));
				return EventType::WarpTarget;
			}
		}
		for (const SpawnPoint& spawnPoint : _spawnPoints) {
			if (std::int32_t(std::floor(spawnPoint.Pos.X / TileSize)) == x && std::int32_t(std::floor((spawnPoint.Pos.Y + 8.0f) / TileSize)) == y) {
				params[0] = spawnPoint.PlayerTypeMask;
				std::memcpy(params + Formats::EventRecord::OriginalParamsOffset, spawnPoint.KeptParams, sizeof(spawnPoint.KeptParams));
				return EventType::LevelStart;
			}
		}
		return EventType::Empty;
	}

	EventType EventMap::GetEventByPosition(float x, float y, std::uint8_t** eventParams)
	{
		return GetEventByPosition((std::int32_t)x / Tiles::TileSet::DefaultTileSize, (std::int32_t)y / Tiles::TileSet::DefaultTileSize, eventParams);
	}

	EventType EventMap::GetEventByPosition(std::int32_t x, std::int32_t y, std::uint8_t** eventParams)
	{
		if (y > _layoutSize.Y) {
			return (_pitType == PitType::InstantDeathPit ? EventType::ModifierDeath : EventType::Empty);
		}

		if (x >= 0 && y >= 0 && y < _layoutSize.Y && x < _layoutSize.X) {
			*eventParams = const_cast<std::uint8_t*>(_eventLayout[x + y * _layoutSize.X].EventParams);	// The callers only read them
			return _eventLayout[x + y * _layoutSize.X].Event;
		}
		return EventType::Empty;
	}

	bool EventMap::HasEventByPosition(std::int32_t x, std::int32_t y) const
	{
		return (x >= 0 && y >= 0 && y < _layoutSize.Y && x < _layoutSize.X &&
			_eventLayout[x + y * _layoutSize.X].Event != EventType::Empty);
	}

	void EventMap::ForEachEvent(Function<bool(EventTile&, std::int32_t, std::int32_t)>&& forEachCallback, bool includeOffGrid) const
	{
		for (std::int32_t y = 0; y < _layoutSize.Y; y++) {
			for (std::int32_t x = 0; x < _layoutSize.X; x++) {
				EventTile* event = _eventLayout.Find(x + y * _layoutSize.X);
				if (event != nullptr && event->Event != EventType::Empty && !forEachCallback(*event, x, y)) {
					return;
				}
			}
		}

		if (includeOffGrid) {
			const std::int32_t gridCount = _layoutSize.X * _layoutSize.Y;
			for (std::size_t i = 0; i < _offGridPositions.size(); i++) {
				EventTile* event = _eventLayout.Find(gridCount + i);
				if (event != nullptr && event->Event != EventType::Empty) {
					const Vector2i& pos = _offGridPositions[i];
					if (!forEachCallback(*event, pos.X / Tiles::TileSet::DefaultTileSize, pos.Y / Tiles::TileSet::DefaultTileSize)) {
						return;
					}
				}
			}
		}
	}

	bool EventMap::IsTileEffectEvent(EventType type)
	{
		switch (type) {
			// What the tile is
			case EventType::SceneryDestruct:
			case EventType::SceneryDestructButtstomp:
			case EventType::SceneryDestructSpeed:
			case EventType::SceneryCollapse:
			case EventType::ModifierVine:
			case EventType::ModifierOneWay:
			case EventType::ModifierHook:
			case EventType::ModifierHPole:
			case EventType::ModifierVPole:
			case EventType::ModifierHurt:
			case EventType::ModifierTube:
			case EventType::ModifierRicochet:
			case EventType::ModifierSlide:
			case EventType::ModifierDeath:
			case EventType::ModifierSetWater:
			case EventType::ModifierLimitCameraView:
			case EventType::ModifierNoClimb:
			// What happens to whoever is in it
			case EventType::AreaStopEnemy:
			case EventType::AreaFloatUp:
			case EventType::AreaHForce:
			case EventType::AreaText:
			case EventType::AreaEndOfLevel:
			case EventType::AreaCallback:
			case EventType::AreaActivateBoss:
			case EventType::AreaFlyOff:
			case EventType::AreaRevertMorph:
			case EventType::AreaMorphToFrog:
			case EventType::AreaNoFire:
			case EventType::AreaWaterBlock:
			case EventType::TriggerArea:
			case EventType::TriggerZone:
			case EventType::RollingRockTrigger:
			case EventType::WarpOrigin:
			case EventType::LightAmbient:
			case EventType::GemStomp:
			// Found on the grid by others
			case EventType::LevelStartMultiplayer:
				return true;
			default:
				return false;
		}
	}

	bool EventMap::IsHurting(float x, float y, Direction dir)
	{
		return IsHurting((std::int32_t)x / Tiles::TileSet::DefaultTileSize, (std::int32_t)y / Tiles::TileSet::DefaultTileSize, dir);
	}

	bool EventMap::IsHurting(std::int32_t x, std::int32_t y, Direction dir)
	{
		return (x >= 0 && y >= 0 && y < _layoutSize.Y && x < _layoutSize.X &&
			_eventLayout[x + y * _layoutSize.X].Event == EventType::ModifierHurt &&
			(_eventLayout[x + y * _layoutSize.X].EventParams[0] & (std::uint8_t)dir) != 0);
	}

	std::int32_t EventMap::GetWarpByPosition(float x, float y)
	{
		std::int32_t tx = (std::int32_t)x / Tiles::TileSet::DefaultTileSize;
		std::int32_t ty = (std::int32_t)y / Tiles::TileSet::DefaultTileSize;
		std::uint8_t* eventParams;
		if (GetEventByPosition(tx, ty, &eventParams) == EventType::WarpOrigin) {
			return eventParams[0];
		} else {
			return -1;
		}
	}

	Vector2f EventMap::GetWarpTarget(std::uint32_t id)
	{
		std::int32_t targetCount = 0;
		for (auto& target : _warpTargets) {
			if (target.Id == id) {
				targetCount++;
			}
		}

		std::int32_t selectedTarget = nCine::Random().Next(0, targetCount);
		for (auto& target : _warpTargets) {
			if (target.Id != id) {
				continue;
			}
			if (selectedTarget == 0) {
				return target.Pos;
			}
			selectedTarget--;
		}

		return Vector2f(-1.0f, -1.0f);
	}

	bool EventMap::IsNearWarpTarget(std::uint32_t id, Vector2f pos, float distance) const
	{
		float distanceSqr = distance * distance;
		for (auto& target : _warpTargets) {
			if (target.Id == id && (target.Pos - pos).SqrLength() <= distanceSqr) {
				return true;
			}
		}
		return false;
	}

	void EventMap::LoadEvents(Formats::LevelReader& reader, Tiles::TileMap& tileMap, const LevelLoadOptions& options)
	{
		if (!_eventLayout.Allocate(std::size_t(_layoutSize.X) * std::size_t(_layoutSize.Y))) {
			LOGE("Cannot allocate the event layout of {}x{} tiles", _layoutSize.X, _layoutSize.Y);
			_loadFailed = true;
			return;
		}

		std::uint8_t difficultyFlag;
		switch (options.Difficulty) {
			case GameDifficulty::Easy:
				difficultyFlag = Formats::EventRecord::Easy;
				break;
			case GameDifficulty::Hard:
				difficultyFlag = Formats::EventRecord::Hard;
				break;
			case GameDifficulty::Normal:
			//case GameDifficulty::Default:
			default:
				difficultyFlag = Formats::EventRecord::Normal;
				break;
		}

		// Which tiles of the grid hold an event in any difficulty, an off-grid event that acts through its tile isn't
		// applied to them, whatever the difficulty (see LoadOffGridEvents())
		BitArray occupiedTiles(ValueInit, std::size_t(_layoutSize.X) * std::size_t(_layoutSize.Y));

		reader.ReadEvents(_layoutSize.X, _layoutSize.Y, [this, &tileMap, &options, difficultyFlag, &occupiedTiles](std::int32_t x, std::int32_t y, const Formats::EventRecord& event) {
			// Starts and warp targets aren't kept in the tiles
			if (event.Type != std::uint16_t(EventType::Empty) && ((event.Flags & Formats::EventRecord::Generator) != 0 ||
				(event.Type != std::uint16_t(EventType::LevelStart) && event.Type != std::uint16_t(EventType::WarpTarget)))) {
				occupiedTiles.set(std::size_t(x) + std::size_t(y) * std::size_t(_layoutSize.X));
			}

			// An event of no difficulty at all is present in all of them, and a multiplayer-only event only in multiplayer
			if ((event.Flags & Formats::EventRecord::DifficultyMask) != 0 && (event.Flags & difficultyFlag) == 0) {
				return;
			}
			if ((event.Flags & Formats::EventRecord::MultiplayerOnly) != 0 && !options.Multiplayer) {
				return;
			}

			EventType eventType = (EventType)event.Type;
			Actors::ActorState actorFlags = (Actors::ActorState)(event.Flags & Formats::EventRecord::Illuminated);
			std::uint8_t eventParams[EventSpawner::SpawnParamsSize];
			std::memcpy(eventParams, event.Params, sizeof(eventParams));

			if ((event.Flags & Formats::EventRecord::Generator) != 0) {
				if (eventType != EventType::Empty) {
					std::uint32_t generatorIdx = (std::uint32_t)_generators.size();
					float timeLeft = ((event.GeneratorFlags & 0x01) != 0 ? event.GeneratorDelay : 0.0f);

					GeneratorInfo& generator = _generators.emplace_back();
					generator.EventPos = x + y * _layoutSize.X;
					generator.Event = eventType;
					std::memcpy(generator.EventParams, eventParams, sizeof(eventParams));
					generator.Delay = event.GeneratorDelay;
					generator.TimeLeft = timeLeft;

					// JJ2 makes the tile of generated destructible scenery destructible as it is, showing the first frame of its
					// animation until it's destroyed (with no delay, it wasn't made again in JJ2+ either)
					switch (eventType) {
						case EventType::SceneryDestruct:
						case EventType::SceneryDestructButtstomp:
						case EventType::SceneryDestructSpeed:
						case EventType::SceneryCollapse:
							tileMap.SetTileEventFlags(x, y, eventType, generator.EventParams);
							break;
						default:
							break;
					}

					Actors::EventParamsWriter(eventParams).SetUint32(0, generatorIdx);
					StoreTileEvent(x, y, EventType::Generator, actorFlags, eventParams);
				}
				return;
			}

			switch (eventType) {
				case EventType::Empty:
					break;

				case EventType::LevelStart: {
					AddSpawnPosition(eventParams[0], x, y, eventParams);
					break;
				}

				case EventType::ModifierOneWay:
				case EventType::ModifierVine:
				case EventType::ModifierHook:
				case EventType::SceneryDestruct:
				case EventType::SceneryDestructButtstomp:
				case EventType::TriggerArea:
				case EventType::SceneryDestructSpeed:
				case EventType::SceneryCollapse:
				case EventType::ModifierHPole:
				case EventType::ModifierVPole: {
					StoreTileEvent(x, y, eventType, actorFlags, eventParams);
					tileMap.SetTileEventFlags(x, y, eventType, eventParams);
					break;
				}

				case EventType::WarpTarget:
					AddWarpTarget(eventParams[0], x, y, eventParams);
					break;

				default:
					StoreTileEvent(x, y, eventType, actorFlags, eventParams);
					break;
			}
		});

		LoadOffGridEvents(reader, tileMap, options, difficultyFlag, occupiedTiles);

		if (_eventLayout.HasOverflowed()) {
			// Every event that isn't a start or a warp target takes a tile of its own, and there can't be more of them
			LOGE("Level has too many events");
			_loadFailed = true;
		}
	}

	void EventMap::LoadOffGridEvents(Formats::LevelReader& reader, Tiles::TileMap& tileMap, const LevelLoadOptions& options, std::uint8_t difficultyFlag,
		BitArray& occupiedTiles)
	{
		SmallVector<Formats::OffGridEventRecord, 0> offGridEvents;
		reader.ReadOffGridEvents(offGridEvents);
		if (offGridEvents.empty() || _layoutSize.X <= 0 || _layoutSize.Y <= 0) {
			return;
		}

		// Every off-grid event gets its tile, even one left out in this difficulty, so the tiles are the same in any
		// difficulty and in multiplayer, where the server refers to objects by their tiles. Positions far outside of
		// the level are left out, the converter stores negative ones wrapped around.
		const std::int32_t gridCount = _layoutSize.X * _layoutSize.Y;
		const std::int32_t maxX = (_layoutSize.X + 64) * Tiles::TileSet::DefaultTileSize;
		const std::int32_t maxY = (_layoutSize.Y + 64) * Tiles::TileSet::DefaultTileSize;
		_offGridPositions.reserve(offGridEvents.size());
		for (const Formats::OffGridEventRecord& record : offGridEvents) {
			if (record.X < (std::uint32_t)maxX && record.Y < (std::uint32_t)maxY) {
				_offGridPositions.push_back(Vector2i((std::int32_t)record.X, (std::int32_t)record.Y));
			}
		}
		std::int32_t count = (std::int32_t)_offGridPositions.size();
		_layoutRows = _layoutSize.Y + (count + _layoutSize.X - 1) / _layoutSize.X;
		if (!_eventLayout.Grow((std::size_t)GetLayoutCount())) {
			LOGE("Cannot allocate {} off-grid events", count);
			_offGridPositions.clear();
			_layoutRows = _layoutSize.Y;
			return;
		}
		BuildOffGridIndex();

		std::int32_t index = 0;
		for (const Formats::OffGridEventRecord& record : offGridEvents) {
			if (record.X >= (std::uint32_t)maxX || record.Y >= (std::uint32_t)maxY) {
				continue;
			}
			const Formats::EventRecord& event = record.Event;
			Vector2i pos = _offGridPositions[index];
			std::int32_t x = index % _layoutSize.X;
			std::int32_t y = _layoutSize.Y + index / _layoutSize.X;
			index++;

			// The tile the position is in, for what acts through its tile rather than through an object. Claimed before
			// the difficulty decides, so the same events get the same tiles in any difficulty, on a server and its clients.
			EventType eventType = (EventType)event.Type;
			std::int32_t tx = pos.X / Tiles::TileSet::DefaultTileSize;
			std::int32_t ty = pos.Y / Tiles::TileSet::DefaultTileSize;
			bool claimsTile = false;
			if ((event.Flags & Formats::EventRecord::Generator) == 0 && IsTileEffectEvent(eventType) &&
				tx >= 0 && ty >= 0 && tx < _layoutSize.X && ty < _layoutSize.Y) {
				const std::size_t cell = std::size_t(tx) + std::size_t(ty) * std::size_t(_layoutSize.X);
				if (!occupiedTiles[cell]) {
					occupiedTiles.set(cell);
					claimsTile = true;
				}
			}

			if ((event.Flags & Formats::EventRecord::DifficultyMask) != 0 && (event.Flags & difficultyFlag) == 0) {
				continue;
			}
			if ((event.Flags & Formats::EventRecord::MultiplayerOnly) != 0 && !options.Multiplayer) {
				continue;
			}

			Actors::ActorState actorFlags = (Actors::ActorState)(event.Flags & Formats::EventRecord::Illuminated);
			std::uint8_t eventParams[EventSpawner::SpawnParamsSize];
			std::memcpy(eventParams, event.Params, sizeof(eventParams));

			if ((event.Flags & Formats::EventRecord::Generator) != 0) {
				if (eventType != EventType::Empty) {
					std::uint32_t generatorIdx = (std::uint32_t)_generators.size();
					GeneratorInfo& generator = _generators.emplace_back();
					generator.EventPos = gridCount + (index - 1);
					generator.Event = eventType;
					std::memcpy(generator.EventParams, eventParams, sizeof(eventParams));
					generator.Delay = event.GeneratorDelay;
					generator.TimeLeft = ((event.GeneratorFlags & 0x01) != 0 ? event.GeneratorDelay : 0.0f);

					std::memset(eventParams, 0, sizeof(eventParams));
					Actors::EventParamsWriter(eventParams).SetUint32(0, generatorIdx);
					StoreTileEvent(x, y, EventType::Generator, actorFlags, eventParams);
				}
				continue;
			}

			switch (eventType) {
				case EventType::Empty:
					break;

				case EventType::LevelStart: {
					// Where it is, offset from it as a start on the grid is from the center of its tile, see AddSpawnPosition()
					if (eventParams[0] != 0) {
						SpawnPoint& target = _spawnPoints.emplace_back();
						target.PlayerTypeMask = eventParams[0];
						target.Pos = Vector2f((float)pos.X - 16.0f, (float)pos.Y - 24.0f);
						std::memcpy(target.KeptParams, eventParams + Formats::EventRecord::OriginalParamsOffset, sizeof(target.KeptParams));
					}
					break;
				}

				case EventType::WarpTarget: {
					WarpTarget& target = _warpTargets.emplace_back();
					target.Id = eventParams[0];
					target.Pos = Vector2f((float)pos.X, (float)pos.Y);
					std::memcpy(target.KeptParams, eventParams + Formats::EventRecord::OriginalParamsOffset, sizeof(target.KeptParams));
					break;
				}

				default:
					if (IsTileEffectEvent(eventType)) {
						// What the tile does, or found on the grid by others, an event there already stays
						if (claimsTile) {
							StoreTileEvent(tx, ty, eventType, actorFlags, eventParams);
							if (eventType != EventType::LevelStartMultiplayer) {
								tileMap.SetTileEventFlags(tx, ty, eventType, eventParams);
							}
						} else {
							LOGW("Off-grid event {} at [{}, {}] has no free tile to be applied to", (std::uint32_t)eventType, pos.X, pos.Y);
						}
					} else {
						StoreTileEvent(x, y, eventType, actorFlags, eventParams);
					}
					break;
			}
		}
	}

	void EventMap::AddWarpTarget(std::uint16_t id, std::int32_t x, std::int32_t y, const std::uint8_t* eventParams)
	{
		WarpTarget& target = _warpTargets.emplace_back();
		target.Id = id;
		target.Pos = Vector2f((float)x * Tiles::TileSet::DefaultTileSize + Tiles::TileSet::DefaultTileSize / 2, (float)y * Tiles::TileSet::DefaultTileSize + Tiles::TileSet::DefaultTileSize / 2);
		if (eventParams != nullptr) {
			std::memcpy(target.KeptParams, eventParams + Formats::EventRecord::OriginalParamsOffset, sizeof(target.KeptParams));
		}
	}

	void EventMap::AddSpawnPosition(std::uint8_t typeMask, std::int32_t x, std::int32_t y, const std::uint8_t* eventParams)
	{
		if (typeMask == 0) {
			return;
		}

		SpawnPoint& target = _spawnPoints.emplace_back();
		target.PlayerTypeMask = typeMask;
		if (eventParams != nullptr) {
			std::memcpy(target.KeptParams, eventParams + Formats::EventRecord::OriginalParamsOffset, sizeof(target.KeptParams));
		}
		target.Pos = Vector2f((float)x * Tiles::TileSet::DefaultTileSize, (float)y * Tiles::TileSet::DefaultTileSize - 8.0f);
	}

	bool EventMap::InitializeFromStream(Stream& src)
	{
		// A state saved before off-grid events were spawned has the grid only, their tiles stay as they're loaded then.
		// The state can come from a save of a level changed since (the user content shadows the shipped levels), so the
		// size is a real check rather than an assumption.
		std::int32_t layoutSize = src.ReadVariableInt32();
		if ((layoutSize != GetLayoutCount() && layoutSize != _layoutSize.X * _layoutSize.Y) || layoutSize < 0 ||
			std::size_t(layoutSize) > _eventLayout.size()) {
			LOGE("Layout size mismatch ({} != {})", layoutSize, GetLayoutCount());
			return false;
		}

		for (std::int32_t i = 0; i < layoutSize; i++) {
			EventTile read = {};
			read.Event = (EventType)src.ReadVariableUint32();
			read.EventFlags = (Actors::ActorState)src.ReadVariableUint32();
			src.Read(read.EventParams, sizeof(read.EventParams));
			EventTile* tile = (read.Event != EventType::Empty ? &_eventLayout.Edit(i) : _eventLayout.Find(i));
			if (tile != nullptr) {
				tile->Event = read.Event;
				tile->EventFlags = read.EventFlags;
				std::memcpy(tile->EventParams, read.EventParams, sizeof(tile->EventParams));
			}
		}
		return true;
	}

	void EventMap::SerializeResumableToStream(Stream& dest, bool fromCheckpoint)
	{
		std::int32_t layoutSize = GetLayoutCount();
		bool useCheckpoint = (fromCheckpoint && _hasRollbackCheckpoint);
		dest.WriteVariableInt32(layoutSize);

		std::size_t nextSaved = 0;
		for (std::int32_t i = 0; i < layoutSize; i++) {
			// A tile missing from the checkpoint list hasn't changed since the checkpoint was taken (only its
			// active bit could have, and that isn't serialized), so the live grid already holds its value
			const EventTile* tile = &_eventLayout[i];
			if (useCheckpoint && nextSaved < _eventLayoutForRollback.size() &&
				_eventLayoutForRollback[nextSaved].TileIndex == (std::uint32_t)i) {
				tile = &_eventLayoutForRollback[nextSaved].Tile;
				nextSaved++;
			}

			dest.WriteVariableUint32((std::uint32_t)tile->Event);
			dest.WriteVariableUint32((std::uint32_t)tile->EventFlags);
			dest.Write(tile->EventParams, sizeof(tile->EventParams)); // TODO: Optimize this
		}
	}

	bool EventMap::InitializeActiveStateFromStream(Stream& src)
	{
		std::int32_t layoutSize = src.ReadVariableInt32();
		if ((layoutSize != GetLayoutCount() && layoutSize != _layoutSize.X * _layoutSize.Y) || layoutSize < 0 ||
			std::size_t(layoutSize) > _eventLayout.size()) {
			LOGE("Layout size mismatch ({} != {})", layoutSize, GetLayoutCount());
			return false;
		}

		std::uint8_t bits = 0;
		for (std::int32_t i = 0; i < layoutSize; i++) {
			if ((i & 7) == 0) {
				bits = src.ReadValue<std::uint8_t>();
			}
			if (EventTile* tile = _eventLayout.Find(i)) {
				tile->IsEventActive = ((bits & (1 << (i & 7))) != 0);
			}
		}
		return true;
	}

	void EventMap::SerializeActiveStateToStream(Stream& dest, bool fromCheckpoint)
	{
		std::int32_t layoutSize = GetLayoutCount();
		bool useCheckpoint = (fromCheckpoint && _hasRollbackCheckpoint);
		dest.WriteVariableInt32(layoutSize);

		std::uint8_t bits = 0;
		for (std::int32_t i = 0; i < layoutSize; i++) {
			if (useCheckpoint ? _eventActiveForRollback[i] : _eventLayout[i].IsEventActive) {
				bits |= (1 << (i & 7));
			}
			if ((i & 7) == 7 || i == layoutSize - 1) {
				dest.WriteValue<std::uint8_t>(bits);
				bits = 0;
			}
		}
	}

	bool EventMap::InitializeSnapshotFromStream(Stream& src)
	{
		// A snapshot taken before off-grid events were spawned has the grid only, see InitializeFromStream()
		std::int32_t layoutSize = src.ReadVariableInt32();
		std::int32_t realLayoutSize = GetLayoutCount();
		if (layoutSize != realLayoutSize && layoutSize != _layoutSize.X * _layoutSize.Y) {
			LOGE("Layout size mismatch ({} != {})", layoutSize, realLayoutSize);
			return false;
		}

		// Only cells that hold a tile of their own are stored, the level is loaded from the same file, so it
		// cannot have any cell the snapshot doesn't know about
		// The counts are checked against the layout, a damaged snapshot could otherwise keep the reading going for long
		std::uint32_t tileCount = src.ReadVariableUint32();
		if (tileCount > std::uint32_t(realLayoutSize)) {
			LOGE("Snapshot has more tiles than the layout ({} > {})", tileCount, realLayoutSize);
			return false;
		}
		for (std::uint32_t i = 0; i < tileCount; i++) {
			std::uint32_t tileIndex = src.ReadVariableUint32();
			EventTile tile;
			ReadEventTile(src, tile);
			if (tileIndex < (std::uint32_t)realLayoutSize) {
				_eventLayout.Edit(tileIndex) = tile;
			}
		}

		std::uint32_t generatorCount = src.ReadVariableUint32();
		if (generatorCount > std::uint32_t(realLayoutSize)) {
			LOGE("Snapshot has more generators than the layout has tiles ({} > {})", generatorCount, realLayoutSize);
			return false;
		}
		for (std::uint32_t i = 0; i < generatorCount; i++) {
			float timeLeft = src.ReadValueAsLE<float>();
			if (i < _generators.size()) {
				_generators[i].TimeLeft = timeLeft;
				_generators[i].SpawnedActor = nullptr;
			}
		}

		_hasRollbackCheckpoint = (src.ReadValue<std::uint8_t>() != 0);
		_eventLayoutForRollback.clear();
		if (_hasRollbackCheckpoint) {
			_eventActiveForRollback.resize(ValueInit, realLayoutSize);
			std::uint8_t bits = 0;
			for (std::int32_t i = 0; i < layoutSize; i++) {
				if ((i & 7) == 0) {
					bits = src.ReadValue<std::uint8_t>();
				}
				_eventActiveForRollback.set(i, (bits & (1 << (i & 7))) != 0);
			}

			std::uint32_t rollbackCount = src.ReadVariableUint32();
			if (rollbackCount > std::uint32_t(realLayoutSize)) {
				LOGE("Snapshot has more saved tiles than the layout ({} > {})", rollbackCount, realLayoutSize);
				_hasRollbackCheckpoint = false;
				return false;
			}
			_eventLayoutForRollback.reserve(rollbackCount);
			for (std::uint32_t i = 0; i < rollbackCount; i++) {
				RollbackTile entry;
				entry.TileIndex = src.ReadVariableUint32();
				ReadEventTile(src, entry.Tile);
				// The rollback writes the tile back by this index, and the list has to stay sorted by it
				if (entry.TileIndex < std::uint32_t(realLayoutSize) &&
					(_eventLayoutForRollback.empty() || _eventLayoutForRollback.back().TileIndex < entry.TileIndex)) {
					_eventLayoutForRollback.push_back(entry);
				}
			}
		}

		return !_eventLayout.HasOverflowed();
	}

	void EventMap::SerializeSnapshotToStream(Stream& dest)
	{
		std::int32_t layoutSize = GetLayoutCount();
		dest.WriteVariableInt32(layoutSize);

		std::uint32_t tileCount = 0;
		for (std::int32_t i = 0; i < layoutSize; i++) {
			if (_eventLayout.Find(i) != nullptr) {
				tileCount++;
			}
		}
		dest.WriteVariableUint32(tileCount);
		for (std::int32_t i = 0; i < layoutSize; i++) {
			if (const EventTile* tile = _eventLayout.Find(i)) {
				dest.WriteVariableUint32((std::uint32_t)i);
				WriteEventTile(dest, *tile);
			}
		}

		dest.WriteVariableUint32((std::uint32_t)_generators.size());
		for (const auto& generator : _generators) {
			dest.WriteValueAsLE<float>(generator.TimeLeft);
		}

		dest.WriteValue<std::uint8_t>(_hasRollbackCheckpoint ? 1 : 0);
		if (_hasRollbackCheckpoint) {
			std::uint8_t bits = 0;
			for (std::int32_t i = 0; i < layoutSize; i++) {
				if (_eventActiveForRollback[i]) {
					bits |= (1 << (i & 7));
				}
				if ((i & 7) == 7 || i == layoutSize - 1) {
					dest.WriteValue<std::uint8_t>(bits);
					bits = 0;
				}
			}

			dest.WriteVariableUint32((std::uint32_t)_eventLayoutForRollback.size());
			for (const auto& entry : _eventLayoutForRollback) {
				dest.WriteVariableUint32(entry.TileIndex);
				WriteEventTile(dest, entry.Tile);
			}
		}
	}

	void EventMap::WriteEventTile(Stream& dest, const EventTile& tile)
	{
		dest.WriteVariableUint32((std::uint32_t)tile.Event);
		dest.WriteVariableUint32((std::uint32_t)tile.EventFlags);
		dest.WriteValue<std::uint8_t>(tile.IsEventActive ? 1 : 0);
		dest.Write(tile.EventParams, sizeof(tile.EventParams));
	}

	void EventMap::ReadEventTile(Stream& src, EventTile& tile)
	{
		tile.Event = (EventType)src.ReadVariableUint32();
		tile.EventFlags = (Actors::ActorState)src.ReadVariableUint32();
		tile.IsEventActive = (src.ReadValue<std::uint8_t>() != 0);
		src.Read(tile.EventParams, sizeof(tile.EventParams));
	}
}