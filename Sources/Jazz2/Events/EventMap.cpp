#include "EventMap.h"
#include "../Tiles/TileMap.h"
#include "../WeatherType.h"

#include "../../nCine/Base/Random.h"
#include "../../nCine/Base/FrameTimer.h"

namespace Jazz2::Events
{
	EventMap::EventMap(ILevelHandler* levelHandler, Vector2i layoutSize, PitType pitType)
		: _levelHandler(levelHandler), _layoutSize(layoutSize), _checkpointCreated(false), _pitType(pitType)
	{
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

		return Vector2f(-1, -1);
	}

	void EventMap::CreateCheckpointForRollback()
	{
		std::memcpy(_eventLayoutForRollback.data(), _eventLayout.data(), _eventLayout.size() * sizeof(EventTile));
	}

	void EventMap::RollbackToCheckpoint()
	{
		for (std::int32_t y = 0; y < _layoutSize.Y; y++) {
			for (std::int32_t x = 0; x < _layoutSize.X; x++) {
				std::int32_t tileID = y * _layoutSize.X + x;
				EventTile& tile = _eventLayout[tileID];
				EventTile& tilePrev = _eventLayoutForRollback[tileID];

				bool respawn = (tilePrev.IsEventActive && !tile.IsEventActive);

				// Rollback tile
				tile = tilePrev;

				if (respawn && tile.Event != EventType::Empty) {
					tile.IsEventActive = true;

					if (tile.Event == EventType::AreaWeather) {
						_levelHandler->SetWeather((WeatherType)tile.EventParams[0], tile.EventParams[1]);
					} else if (tile.Event != EventType::Generator) {
						Actors::ActorState flags = Actors::ActorState::IsCreatedFromEventMap | tile.EventFlags;
						std::shared_ptr<Actors::ActorBase> actor = _levelHandler->EventSpawner()->SpawnEvent(tile.Event, tile.EventParams, flags, x, y, ILevelHandler::MainPlaneZ);
						if (actor != nullptr) {
							_levelHandler->AddActor(actor);
						}
					}
				}
			}
		}
	}

	void EventMap::StoreTileEvent(std::int32_t x, std::int32_t y, EventType eventType, Actors::ActorState eventFlags, std::uint8_t* tileParams)
	{
		if (eventType == EventType::Empty && (x < 0 || y < 0 || x >= _layoutSize.X || y >= _layoutSize.Y)) {
			return;
		}

		EventTile& previousEvent = _eventLayout[x + y * _layoutSize.X];

		EventTile newEvent = { };
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
		auto eventSpawner = _levelHandler->EventSpawner();

		// TODO
		//ContentResolver::Get().SuspendAsync();

		// Preload all events
		for (auto& tile : _eventLayout) {
			// TODO: Exclude also some modifiers here ?
			if (tile.Event != EventType::Empty && tile.Event != EventType::Generator && tile.Event != EventType::AreaWeather) {
				eventSpawner->PreloadEvent(tile.Event, tile.EventParams);
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

					generator.SpawnedActor = _levelHandler->EventSpawner()->SpawnEvent(generator.Event,
						generator.EventParams, Actors::ActorState::IsFromGenerator, x, y, ILevelHandler::SpritePlaneZ);
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
		auto tiles = _levelHandler->TileMap();
		if (tiles == nullptr) {
			return;
		}

		std::int32_t x1 = std::max(0, tx1);
		std::int32_t x2 = std::min(_layoutSize.X - 1, tx2);
		std::int32_t y1 = std::max(0, ty1);
		std::int32_t y2 = std::min(_layoutSize.Y - 1, ty2);

		for (std::int32_t x = x1; x <= x2; x++) {
			for (std::int32_t y = y1; y <= y2; y++) {
				auto& tile = _eventLayout[x + y * _layoutSize.X];
				if (!tile.IsEventActive && tile.Event != EventType::Empty) {
					tile.IsEventActive = true;

					if (tile.Event == EventType::AreaWeather) {
						_levelHandler->SetWeather((WeatherType)tile.EventParams[0], tile.EventParams[1]);
					} else if (tile.Event != EventType::Generator) {
						Actors::ActorState flags = Actors::ActorState::IsCreatedFromEventMap | tile.EventFlags;
						if (allowAsync) {
							flags |= Actors::ActorState::Async;
						}

						std::shared_ptr<Actors::ActorBase> actor = _levelHandler->EventSpawner()->SpawnEvent(tile.Event, tile.EventParams, flags, x, y, ILevelHandler::SpritePlaneZ);
						if (actor != nullptr) {
							_levelHandler->AddActor(actor);
						}
					}
				}
			}
		}

		if (!_checkpointCreated) {
			// Create checkpostd::int32_t after first call to ActivateEvents() to avoid duplication of objects that are spawned near player spawn
			std::memcpy(_eventLayoutForRollback.data(), _eventLayout.data(), _eventLayout.size() * sizeof(EventTile));
			_checkpointCreated = true;
		}
	}

	void EventMap::Deactivate(std::int32_t x, std::int32_t y)
	{
		if (HasEventByPosition(x, y)) {
			_eventLayout[x + y * _layoutSize.X].IsEventActive = false;
		}
	}

	void EventMap::ResetGenerator(std::int32_t tx, std::int32_t ty)
	{
		// Linked actor was deactivated, but not destroyed
		// Reset its generator, so it can be respawned immediately
		std::uint32_t generatorIdx = *(std::uint32_t*)_eventLayout[tx + ty * _layoutSize.X].EventParams;
		_generators[generatorIdx].TimeLeft = 0.0f;
		_generators[generatorIdx].SpawnedActor = nullptr;
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
			*eventParams = _eventLayout[x + y * _layoutSize.X].EventParams;
			return _eventLayout[x + y * _layoutSize.X].Event;
		}
		return EventType::Empty;
	}

	bool EventMap::HasEventByPosition(std::int32_t x, std::int32_t y)
	{
		return (x >= 0 && y >= 0 && y < _layoutSize.Y && x < _layoutSize.X && _eventLayout[x + y * _layoutSize.X].Event != EventType::Empty);
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

	void EventMap::ReadEvents(Stream& s, const std::unique_ptr<Tiles::TileMap>& tileMap, GameDifficulty difficulty)
	{
		_eventLayout.resize(_layoutSize.X * _layoutSize.Y);
		_eventLayoutForRollback.resize(_layoutSize.X * _layoutSize.Y);

		std::uint8_t difficultyBit;
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

		for (std::int32_t y = 0; y < _layoutSize.Y; y++) {
			for (std::int32_t x = 0; x < _layoutSize.X; x++) {
				std::uint16_t eventType = s.ReadValue<std::uint16_t>();
				std::uint8_t eventFlags = s.ReadValue<std::uint8_t>();
				std::uint8_t eventParams[16];

				// ToDo: Remove inlined constants

				// Flag 0x02: Generator
				std::uint8_t generatorFlags, generatorDelay;
				if ((eventFlags & 0x02) != 0) {
					//eventFlags ^= 0x02;
					generatorFlags = s.ReadValue<std::uint8_t>();
					generatorDelay = s.ReadValue<std::uint8_t>();
				} else {
					generatorFlags = 0;
					generatorDelay = 0;
				}

				// Flag 0x01: No params provided
				if ((eventFlags & 0x01) == 0) {
					eventFlags ^= 0x01;
					s.Read(eventParams, sizeof(eventParams));
				} else {
					memset(eventParams, 0, sizeof(eventParams));
				}

				Actors::ActorState actorFlags = (Actors::ActorState)(eventFlags & 0x04);

				// Flag 0x02: Generator
				if ((eventFlags & 0x02) != 0) {
					if ((EventType)eventType != EventType::Empty && (eventFlags & (0x01 << difficultyBit)) != 0 && (eventFlags & 0x80) == 0) {
						std::uint32_t generatorIdx = (std::uint32_t)_generators.size();
						float timeLeft = ((generatorFlags & 0x01) != 0 ? generatorDelay : 0.0f);

						GeneratorInfo& generator = _generators.emplace_back();
						generator.EventPos = x + y * _layoutSize.X;
						generator.Event = (EventType)eventType;
						std::memcpy(generator.EventParams, eventParams, sizeof(eventParams));
						generator.Delay = generatorDelay;
						generator.TimeLeft = timeLeft;

						*(std::uint32_t*)eventParams = generatorIdx;
						StoreTileEvent(x, y, EventType::Generator, actorFlags, eventParams);
					}
					continue;
				}

				// If the difficulty bytes for the event don't match the selected difficulty, don't add anything to the event map
				// Additionally, never show events that are multiplayer-only
				if (eventFlags == 0 || ((eventFlags & (0x01 << difficultyBit)) != 0 && (eventFlags & 0x80) == 0)) {
					switch ((EventType)eventType) {
						case EventType::Empty:
							break;

						case EventType::LevelStart: {
							AddSpawnPosition(eventParams[0], x, y);
							break;
						}

#if MULTIPLAYER && SERVER
						case EventType::LevelStartMultiplayer: {
							// TODO: check parameters
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
							StoreTileEvent(x, y, (EventType)eventType, actorFlags, eventParams);
							tileMap->SetTileEventFlags(x, y, (EventType)eventType, eventParams);
							break;
						}

						case EventType::WarpTarget:
							AddWarpTarget(eventParams[0], x, y);
							break;

						default:
							StoreTileEvent(x, y, (EventType)eventType, actorFlags, eventParams);
							break;
					}
				}
			}
		}
	}

	void EventMap::AddWarpTarget(std::uint16_t id, std::int32_t x, std::int32_t y)
	{
		WarpTarget& target = _warpTargets.emplace_back();
		target.Id = id;
		target.Pos = Vector2f(x * Tiles::TileSet::DefaultTileSize + Tiles::TileSet::DefaultTileSize / 2, y * Tiles::TileSet::DefaultTileSize + Tiles::TileSet::DefaultTileSize / 2);
	}

	void EventMap::AddSpawnPosition(std::uint8_t typeMask, std::int32_t x, std::int32_t y)
	{
		if (typeMask == 0) {
			return;
		}

		SpawnPoint& target = _spawnPoints.emplace_back();
		target.PlayerTypeMask = typeMask;
		target.Pos = Vector2f(x * Tiles::TileSet::DefaultTileSize, y * Tiles::TileSet::DefaultTileSize - 8);
	}
}