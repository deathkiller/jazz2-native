#pragma once

#include "EventSpawner.h"
#include "../GameDifficulty.h"
#include "../PitType.h"

#include "../../nCine/IO/IFileStream.h"

namespace Jazz2::Events
{
	class EventMap
	{
	public:
		EventMap(ILevelHandler* levelHandler, Vector2i layoutSize, PitType pitType);

		Vector2f GetSpawnPosition(PlayerType type);
		void CreateCheckpointForRollback();
		void RollbackToCheckpoint();

		void StoreTileEvent(int x, int y, EventType eventType, Actors::ActorState eventFlags = Actors::ActorState::None, uint8_t* tileParams = nullptr);
		void PreloadEventsAsync();

		void ProcessGenerators(float timeMult);
		void ActivateEvents(int tx1, int ty1, int tx2, int ty2, bool allowAsync);
		void Deactivate(int x, int y);
		void ResetGenerator(int tx, int ty);

		EventType GetEventByPosition(float x, float y, uint8_t** eventParams);
		EventType GetEventByPosition(int x, int y, uint8_t** eventParams);
		bool HasEventByPosition(int x, int y);
		int GetWarpByPosition(float x, float y);
		Vector2f GetWarpTarget(uint32_t id);

		void ReadEvents(IFileStream& s, const std::unique_ptr<Tiles::TileMap>& tileMap, GameDifficulty difficulty);
		void AddWarpTarget(uint16_t id, int x, int y);
		void AddSpawnPosition(uint8_t typeMask, int x, int y);

	private:
		struct EventTile {
			EventType Event;
			Actors::ActorState EventFlags;
			uint8_t EventParams[16];
			bool IsEventActive;
		};

		struct GeneratorInfo {
			int EventPos;

			EventType Event;
			uint8_t EventParams[16];
			uint8_t Delay;
			float TimeLeft;

			std::shared_ptr<Actors::ActorBase> SpawnedActor;
		};

		struct SpawnPoint {
			uint8_t PlayerTypeMask;
			Vector2f Pos;
		};

		struct WarpTarget {
			uint16_t Id;
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
		bool _checkpointCreated;
	};
}