#include "EventMap.h"
#include "../Tiles/TileMap.h"

#include "../../nCine/Base/Random.h"
#include "../../nCine/Base/FrameTimer.h"

namespace Jazz2::Events
{
	EventMap::EventMap(ILevelHandler* levelHandler, Vector2i layoutSize)
		:
		_levelHandler(levelHandler),
		_layoutSize(layoutSize)
	{
	}

	Vector2f EventMap::GetSpawnPosition(PlayerType type)
	{
		int targetCount = 0;
		for (auto& target : _spawnPoints) {
			if ((target.PlayerTypeMask & (1 << ((int)type - 1))) != 0) {
				targetCount++;
			}
		}

		if (targetCount > 0) {
			int selectedTarget = nCine::Random().Next(0, targetCount);
			for (auto& target : _spawnPoints) {
				if ((target.PlayerTypeMask & (1 << ((int)type - 1))) == 0) {
					continue;
				}
				if (selectedTarget == 0) {
					return target.Pos;
				}
				selectedTarget--;
			}
		}

		return Vector2f(-1, -1);
	}

	void EventMap::StoreTileEvent(int x, int y, EventType eventType, ActorFlags eventFlags, uint8_t* tileParams)
	{
		if (eventType == EventType::Empty && (x < 0 || y < 0 || x >= _layoutSize.X || y >= _layoutSize.Y)) {
			return;
		}

		EventTile& previousEvent = _eventLayout[x + y * _layoutSize.X];

		EventTile newEvent = { };
		newEvent.EventType = eventType,
		newEvent.EventFlags = eventFlags,
		newEvent.IsEventActive = (previousEvent.EventType == eventType && previousEvent.IsEventActive);

		// Store event parameters
		int i = 0;
		if (tileParams != nullptr) {
			memcpy(newEvent.EventParams, tileParams, sizeof(newEvent.EventParams));
		}

		previousEvent = newEvent;
	}

	void EventMap::PreloadEventsAsync()
	{
		auto eventSpawner = _levelHandler->EventSpawner();

		// TODO
		//ContentResolver::Current().SuspendAsync();

		// Preload all events
		for (auto& tile : _eventLayout) {
			// ToDo: Exclude also some modifiers here ?
			if (tile.EventType != EventType::Empty && tile.EventType != EventType::Generator && tile.EventType != EventType::AreaWeather) {
				eventSpawner->PreloadEvent(tile.EventType, tile.EventParams);
			}
		}

		// Preload all generators
		for (auto& generator : _generators) {
			eventSpawner->PreloadEvent(generator.EventType, generator.EventParams);
		}

		// Don't wait for finalization of resources, it will be done in a few next frames
		//ContentResolver::Current().ResumeAsync();
	}

	void EventMap::ProcessGenerators(float timeMult)
	{
		for (auto& generator : _generators) {
			if (!_eventLayout[generator.EventPos].IsEventActive) {
				// Generator is inactive (and recharging)
				generator.TimeLeft -= timeMult;
			} else if (generator.SpawnedActor == nullptr /*|| generator.SpawnedActor.Scene == null*/) {
				// TODO: check if actor is still alive
				if (generator.TimeLeft <= 0.0f) {
					// Generator is active and is ready to spawn new actor
					generator.TimeLeft = generator.Delay * FrameTimer::FramesPerSecond;

					int x = generator.EventPos % _layoutSize.X;
					int y = generator.EventPos / _layoutSize.X;

					std::shared_ptr<ActorBase> actor = _levelHandler->EventSpawner()->SpawnEvent(generator.EventType,
						generator.EventParams, ActorFlags::IsFromGenerator, x, y, ILevelHandler::MainPlaneZ);
					if (actor != nullptr) {
						_levelHandler->AddActor(actor);
						generator.SpawnedActor = actor;
					}
				} else {
					// Generator is active and recharging
					generator.TimeLeft -= timeMult;
				}
			}
		}
	}

	void EventMap::ActivateEvents(int tx1, int ty1, int tx2, int ty2, bool allowAsync)
	{
		auto tiles = _levelHandler->TileMap();
		if (tiles == nullptr) {
			return;
		}

		int x1 = std::max(0, tx1);
		int x2 = std::min(_layoutSize.X - 1, tx2);
		int y1 = std::max(0, ty1);
		int y2 = std::min(_layoutSize.Y - 1, ty2);

		for (int x = x1; x <= x2; x++) {
			for (int y = y1; y <= y2; y++) {
				auto& tile = _eventLayout[x + y * _layoutSize.X];
				if (!tile.IsEventActive && tile.EventType != EventType::Empty) {
					tile.IsEventActive = true;

					// TODO
					/*if (tile.EventType == EventType.AreaWeather) {
						_levelHandler->ApplyWeather((LevelHandler.WeatherType)tile.EventParams[0], tile.EventParams[1], tile.EventParams[2] != 0);
					} else*/ if (tile.EventType != EventType::Generator) {
						ActorFlags flags = ActorFlags::IsCreatedFromEventMap | tile.EventFlags;
						if (allowAsync) {
							flags |= ActorFlags::Async;
						}

						std::shared_ptr<ActorBase> actor = _levelHandler->EventSpawner()->SpawnEvent(tile.EventType, tile.EventParams, flags, x, y, ILevelHandler::MainPlaneZ);
						if (actor != nullptr) {
							_levelHandler->AddActor(actor);
						}
					}
				}
			}
		}
	}

	void EventMap::Deactivate(int x, int y)
	{
		if (HasEventByPosition(x, y)) {
			_eventLayout[x + y * _layoutSize.X].IsEventActive = false;
		}
	}

	void EventMap::ResetGenerator(int tx, int ty)
	{
		// Linked actor was deactivated, but not destroyed
		// Reset its generator, so it can be respawned immediately
		uint16_t generatorIdx = *(uint16_t*)_eventLayout[tx + ty * _layoutSize.X].EventParams;
		_generators[generatorIdx].TimeLeft = 0.0f;
	}

	EventType EventMap::GetEventByPosition(float x, float y, uint8_t** eventParams)
	{
		return GetEventByPosition((int)x / 32, (int)y / 32, eventParams);
	}

	EventType EventMap::GetEventByPosition(int x, int y, uint8_t** eventParams)
	{
		if (y > _layoutSize.Y) {
			return EventType::ModifierDeath;
		}

		if (HasEventByPosition(x, y)) {
			*eventParams = _eventLayout[x + y * _layoutSize.X].EventParams;
			return _eventLayout[x + y * _layoutSize.X].EventType;
		}
		return EventType::Empty;
	}

	bool EventMap::HasEventByPosition(int x, int y)
	{
		return (x >= 0 && y >= 0 && y < _layoutSize.Y && x < _layoutSize.X && _eventLayout[x + y * _layoutSize.X].EventType != EventType::Empty);
	}

	bool EventMap::IsHurting(float x, float y)
	{
		// ToDo: Implement all JJ2+ parameters (directional hurt events)
		int tx = (int)x / 32;
		int ty = (int)y / 32;

		uint8_t* eventParams;
		if (GetEventByPosition(tx, ty, &eventParams) != EventType::ModifierHurt) {
			return false;
		}

		return !_levelHandler->TileMap()->IsTileEmpty(tx, ty);
	}

	int EventMap::IsPole(float x, float y)
	{
		uint8_t* eventParams;
		EventType e = GetEventByPosition((int)x / 32, (int)y / 32, &eventParams);
		return (e == EventType::ModifierHPole ? 2 : (e == EventType::ModifierVPole ? 1 : 0));
	}

	int EventMap::GetWarpByPosition(float x, float y)
	{
		int tx = (int)x / 32;
		int ty = (int)y / 32;
		uint8_t* eventParams;
		if (GetEventByPosition(tx, ty, &eventParams) == EventType::WarpOrigin) {
			return eventParams[0];
		} else {
			return -1;
		}
	}

	Vector2f EventMap::GetWarpTarget(uint32_t id)
	{
		int targetCount = 0;
		for (auto& target : _warpTargets) {
			if (target.Id == id) {
				targetCount++;
			}
		}

		int selectedTarget = nCine::Random().Next(0, targetCount);
		for (auto& target : _warpTargets) {
			if (target.Id != id) {
				continue;
			}
			if (selectedTarget == 0) {
				return target.Pos;
			}
			selectedTarget--;
		}

		return Vector2f(-1, -1);
	}

	void EventMap::ReadEvents(const std::unique_ptr<IFileStream>& s, const std::unique_ptr<Tiles::TileMap>& tileMap, uint32_t layoutVersion, GameDifficulty difficulty)
	{
		s->Open(FileAccessMode::Read);

		if (s->GetSize() < 4) {
			return;
		}

		int32_t width = s->ReadValue<int32_t>();
		int32_t height = s->ReadValue<int32_t>();

		if (_layoutSize.X != width || _layoutSize.Y != height) {
			return;
		}

		_eventLayout.resize(width * height);

		uint8_t difficultyBit;
		switch (difficulty) {
			case GameDifficulty::Easy:
				difficultyBit = 4;
				break;
			case GameDifficulty::Hard:
				difficultyBit = 6;
				break;
			case GameDifficulty::Normal:
			//case GameDifficulty::Default:
			//case GameDifficulty::Multiplayer:
			default:
				difficultyBit = 5;
				break;
		}

		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				uint16_t eventID = s->ReadValue<uint16_t>();
				uint8_t flags = s->ReadValue<uint8_t>();
				uint8_t eventParams[16];

				// ToDo: Remove inlined constants

				// Flag 0x02: Generator
				uint8_t generatorFlags, generatorDelay;
				if ((flags & 0x02) != 0) {
					//eventFlags ^= 0x02;
					generatorFlags = s->ReadValue<uint8_t>();
					generatorDelay = s->ReadValue<uint8_t>();
				} else {
					generatorFlags = 0;
					generatorDelay = 0;
				}

				// Flag 0x01: No params provided
				if ((flags & 0x01) == 0) {
					flags ^= 0x01;
					s->Read(eventParams, sizeof(eventParams));
				} else {
					memset(eventParams, 0, sizeof(eventParams));
				}

				ActorFlags eventFlags = (ActorFlags)(flags & 0x04);

				// Flag 0x02: Generator
				if ((flags & 0x02) != 0) {
					if ((EventType)eventID != EventType::Empty && (flags & (0x01 << difficultyBit)) != 0 && (flags & 0x80) == 0) {
						uint16_t generatorIdx = (uint16_t)_generators.size();
						float timeLeft = ((generatorFlags & 0x01) != 0 ? generatorDelay : 0.0f);

						GeneratorInfo& generator = _generators.emplace_back();
						generator.EventPos = x + y * _layoutSize.X;
						generator.EventType = (EventType)eventID;
						memcpy(generator.EventParams, eventParams, sizeof(eventParams));
						generator.Delay = generatorDelay;
						generator.TimeLeft = timeLeft;

						*(uint16_t*)eventParams = generatorIdx;
						StoreTileEvent(x, y, EventType::Generator, eventFlags, eventParams);
					}
					continue;
				}

				// If the difficulty bytes for the event don't match the selected difficulty, don't add anything to the event map
				// Additionally, never show events that are multiplayer-only
				if (flags == 0 || ((flags & (0x01 << difficultyBit)) != 0 && (flags & 0x80) == 0)) {
					switch ((EventType)eventID) {
						case EventType::Empty:
							break;

						case EventType::LevelStart: {
							AddSpawnPosition(eventParams[0], x, y);
							break;
						}

#if MULTIPLAYER && SERVER
						case EventType::LevelStartMultiplayer: {
							// ToDo: check parameters
							spawnPositionsForMultiplayer.Add(new Vector2(32 * x + 16, 32 * y + 16 - 8));
							break;
						}
#endif

						case EventType::ModifierOneWay:
						case EventType::ModifierVine:
						case EventType::ModifierHook:
						case EventType::ModifierHurt:
						case EventType::SceneryDestruct:
						case EventType::SceneryDestructButtstomp:
						case EventType::TriggerArea:
						case EventType::SceneryDestructSpeed:
						case EventType::SceneryCollapse:
						case EventType::ModifierHPole:
						case EventType::ModifierVPole: {
							StoreTileEvent(x, y, (EventType)eventID, eventFlags, eventParams);
							tileMap->SetTileEventFlags(x, y, (EventType)eventID, eventParams);
							break;
						}

						case EventType::WarpTarget:
							AddWarpTarget(eventParams[0], x, y);
							break;
						case EventType::LightReset:
							// TODO
							//eventParams[0] = (uint16_t)_levelHandler.AmbientLightDefault;
							//StoreTileEvent(x, y, EventType::LightSet, eventFlags, eventParams);
							break;

						default:
							StoreTileEvent(x, y, (EventType)eventID, eventFlags, eventParams);
							break;
					}
				}
			}
		}

		// TODO
		//Array.Copy(eventLayout, eventLayoutForRollback, eventLayout.Length);
	}

	void EventMap::StoreTileEvent(int x, int y, EventType eventType, ActorFlags eventFlags, uint16_t* tileParams)
	{
		if (eventType == EventType::Empty && (x < 0 || y < 0 || x >= _layoutSize.X || y >= _layoutSize.Y )) {
			return;
		}

		auto& previousEvent = _eventLayout[x + y * _layoutSize.X];

		EventTile newEvent = { };
		newEvent.EventType = eventType;
		newEvent.EventFlags = eventFlags;
		newEvent.IsEventActive = (previousEvent.EventType == eventType && previousEvent.IsEventActive);

		// Store event parameters
		if (tileParams != nullptr) {
			memcpy(newEvent.EventParams, tileParams, sizeof(newEvent.EventParams));
		}

		previousEvent = newEvent;
	}

	void EventMap::AddWarpTarget(uint16_t id, int x, int y)
	{
		WarpTarget& target = _warpTargets.emplace_back();
		target.Id = id;
		target.Pos = Vector2f(x * Tiles::TileSet::DefaultTileSize + Tiles::TileSet::DefaultTileSize / 2, y * Tiles::TileSet::DefaultTileSize + 12);
	}

	void EventMap::AddSpawnPosition(uint8_t typeMask, int x, int y)
	{
		if (typeMask == 0) {
			return;
		}

		SpawnPoint& target = _spawnPoints.emplace_back();
		target.PlayerTypeMask = typeMask;
		target.Pos = Vector2f(x * Tiles::TileSet::DefaultTileSize + Tiles::TileSet::DefaultTileSize / 2, y * Tiles::TileSet::DefaultTileSize - 8);
	}
}