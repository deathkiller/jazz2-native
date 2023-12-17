#pragma once

#include "EventSpawner.h"
#include "../GameDifficulty.h"
#include "../PitType.h"

#include <IO/Stream.h>

using namespace Death::IO;

namespace Jazz2::Tiles
{
	class TileMap;
}

namespace Jazz2::Events
{
	class EventMap
	{
	public:
		struct EventTile {
			EventType Event;
			Actors::ActorState EventFlags;
			std::uint8_t EventParams[EventSpawner::SpawnParamsSize];
			bool IsEventActive;
		};

		EventMap(const Vector2i& layoutSize);

		void SetLevelHandler(ILevelHandler* levelHandler);
		Vector2i GetSize() const;
		PitType GetPitType() const;
		void SetPitType(PitType value);

		Vector2f GetSpawnPosition(PlayerType type);
		void CreateCheckpointForRollback();
		void RollbackToCheckpoint();

		void StoreTileEvent(std::int32_t x, std::int32_t y, EventType eventType, Actors::ActorState eventFlags = Actors::ActorState::None, std::uint8_t* tileParams = nullptr);
		void PreloadEventsAsync();

		void ProcessGenerators(float timeMult);
		void ActivateEvents(std::int32_t tx1, std::int32_t ty1, std::int32_t tx2, std::int32_t ty2, bool allowAsync);
		void Deactivate(std::int32_t x, std::int32_t y);
		void ResetGenerator(std::int32_t tx, std::int32_t ty);

		const EventTile& GetEventTile(std::int32_t x, std::int32_t y) const;
		EventType GetEventByPosition(float x, float y, std::uint8_t** eventParams);
		EventType GetEventByPosition(std::int32_t x, std::int32_t y, std::uint8_t** eventParams);
		bool HasEventByPosition(std::int32_t x, std::int32_t y);
		std::int32_t GetWarpByPosition(float x, float y);
		Vector2f GetWarpTarget(std::uint32_t id);

		void ReadEvents(Stream& s, const std::unique_ptr<Tiles::TileMap>& tileMap, GameDifficulty difficulty);
		void AddWarpTarget(std::uint16_t id, std::int32_t x, std::int32_t y);
		void AddSpawnPosition(std::uint8_t typeMask, std::int32_t x, std::int32_t y);

		void InitializeFromStream(Stream& src);
		void SerializeResumableToStream(Stream& dest);

	private:
		struct GeneratorInfo {
			std::int32_t EventPos;

			EventType Event;
			std::uint8_t EventParams[EventSpawner::SpawnParamsSize];
			std::uint8_t Delay;
			float TimeLeft;

			std::shared_ptr<Actors::ActorBase> SpawnedActor;
		};

		struct SpawnPoint {
			std::uint8_t PlayerTypeMask;
			Vector2f Pos;
		};

		struct WarpTarget {
			std::uint16_t Id;
			Vector2f Pos;
		};

		ILevelHandler* _levelHandler;
		Vector2i _layoutSize;
		PitType _pitType;
		SmallVector<EventTile, 0> _eventLayout;
		SmallVector<EventTile, 0> _eventLayoutForRollback;
		SmallVector<GeneratorInfo, 0> _generators;
		SmallVector<SpawnPoint, 0> _spawnPoints;
		SmallVector<WarpTarget, 0> _warpTargets;
	};
}