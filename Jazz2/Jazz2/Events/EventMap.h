#pragma once

#include "EventSpawner.h"
#include "../LevelInitialization.h"

#include "../../nCine/IO/IFileStream.h"

namespace Jazz2::Events
{
	class EventMap
	{
	public:
		EventMap(ILevelHandler* levelHandler, Vector2i layoutSize);

		Vector2f GetSpawnPosition(PlayerType type);

		void StoreTileEvent(int x, int y, EventType eventType, ActorFlags eventFlags = ActorFlags::None, uint8_t* tileParams = nullptr);
		void PreloadEventsAsync();

		void ProcessGenerators(float timeMult);
		void ActivateEvents(int tx1, int ty1, int tx2, int ty2, bool allowAsync);
		void Deactivate(int x, int y);
		void ResetGenerator(int tx, int ty);

		EventType GetEventByPosition(float x, float y, uint8_t** eventParams);
		EventType GetEventByPosition(int x, int y, uint8_t** eventParams);
		bool HasEventByPosition(int x, int y);
		bool IsHurting(float x, float y);
		int IsPole(float x, float y);
		int GetWarpByPosition(float x, float y);
		Vector2f GetWarpTarget(uint32_t id);

		void ReadEvents(const std::unique_ptr<IFileStream>& s, uint32_t layoutVersion, GameDifficulty difficulty);
		void StoreTileEvent(int x, int y, EventType eventType, ActorFlags eventFlags, uint16_t* tileParams);
		void AddWarpTarget(uint16_t id, int x, int y);
		void AddSpawnPosition(uint8_t typeMask, int x, int y);

	private:
		struct EventTile {
			EventType EventType;
			ActorFlags EventFlags;
			uint8_t EventParams[16];
			bool IsEventActive;
		};

		struct GeneratorInfo {
			int EventPos;

			EventType EventType;
			uint8_t EventParams[16];
			byte Delay;
			float TimeLeft;

			std::shared_ptr<ActorBase> SpawnedActor;
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
		Death::SmallVector<EventTile, 0> _eventLayout;
		Death::SmallVector<GeneratorInfo, 0> _generators;
		Death::SmallVector<SpawnPoint, 0> _spawnPoints;
		Death::SmallVector<WarpTarget, 0> _warpTargets;
	};
}