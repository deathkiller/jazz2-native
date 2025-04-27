#pragma once

#include "EventSpawner.h"
#include "../Direction.h"
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
	/** @brief Represents event map, spawns triggered objects */
	class EventMap // : public IResumable
	{
	public:
		/** @brief Represents an event tile */
		struct EventTile {
			/** @brief Event type */
			EventType Event;
			/** @brief Event flags */
			Actors::ActorState EventFlags;
			/** @brief Event parameters */
			std::uint8_t EventParams[EventSpawner::SpawnParamsSize];
			/** @brief Whether the event is active */
			bool IsEventActive;
		};

		EventMap(Vector2i layoutSize);

		/** @brief Sets owner of the event map */
		void SetLevelHandler(ILevelHandler* levelHandler);
		/** @brief Returns size of event map in tiles */
		Vector2i GetSize() const;
		/** @brief Returns pit type */
		PitType GetPitType() const;
		/** @brief Sets pit type */
		void SetPitType(PitType value);

		/** @brief Returns spawn position for specified player type */
		Vector2f GetSpawnPosition(PlayerType type);
		/** @brief Creates a checkpoint for eventual rollback */
		void CreateCheckpointForRollback();
		/** @brief Rolls back to the last checkpoint */
		void RollbackToCheckpoint();

		/** @brief Stores tile event description */
		void StoreTileEvent(std::int32_t x, std::int32_t y, EventType eventType, Actors::ActorState eventFlags = Actors::ActorState::None, std::uint8_t* tileParams = nullptr);
		/** @brief Preloads assets of all contained events */
		void PreloadEventsAsync();

		/** @brief Processes all generators */
		void ProcessGenerators(float timeMult);
		/** @brief Activates all inactive events in specified tile restangle */
		void ActivateEvents(std::int32_t tx1, std::int32_t ty1, std::int32_t tx2, std::int32_t ty2, bool allowAsync);
		/** @brief Deactivates event on specified tile position */
		void Deactivate(std::int32_t x, std::int32_t y);
		/** @brief Resets generator on specified tile position */
		void ResetGenerator(std::int32_t tx, std::int32_t ty);

		/** @brief Returns event description of specified tile position */
		const EventTile& GetEventTile(std::int32_t x, std::int32_t y) const;
		/** @brief Returns event type on specified position */
		EventType GetEventByPosition(float x, float y, std::uint8_t** eventParams);
		/** @overload */
		EventType GetEventByPosition(std::int32_t x, std::int32_t y, std::uint8_t** eventParams);
		/** @brief Returns `true` if specified tile position contains an event */
		bool HasEventByPosition(std::int32_t x, std::int32_t y) const;
		/** @brief Calls specified callback function for each event found by type */
		void ForEachEvent(EventType eventType, Function<bool(const EventTile&, std::int32_t, std::int32_t)>&& forEachCallback) const;
		/** @brief Returns `true` if specified position contains hurt event */
		bool IsHurting(float x, float y, Direction dir);
		/** @overload */
		bool IsHurting(std::int32_t x, std::int32_t y, Direction dir);
		/** @brief Returns ID of warp on specified position */
		std::int32_t GetWarpByPosition(float x, float y);
		/** @brief Returns target position for specified warp */
		Vector2f GetWarpTarget(std::uint32_t id);

		/** @brief Reads event layer data from stream */
		void ReadEvents(Stream& s, const std::unique_ptr<Tiles::TileMap>& tileMap, GameDifficulty difficulty);
		/** @brief Adds target position for specified warp */
		void AddWarpTarget(std::uint16_t id, std::int32_t x, std::int32_t y);
		/** @brief Adds spawn position with specified player type mask */
		void AddSpawnPosition(std::uint8_t typeMask, std::int32_t x, std::int32_t y);

		/** @brief Initializes event map state from a stream */
		void InitializeFromStream(Stream& src);
		/** @brief Serializes event map state to a stream */
		void SerializeResumableToStream(Stream& dest);

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
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
#endif

		ILevelHandler* _levelHandler;
		Vector2i _layoutSize;
		PitType _pitType;
		std::unique_ptr<EventTile[]> _eventLayout;
		std::unique_ptr<EventTile[]> _eventLayoutForRollback;
		SmallVector<GeneratorInfo, 0> _generators;
		SmallVector<SpawnPoint, 0> _spawnPoints;
		SmallVector<WarpTarget, 0> _warpTargets;
	};
}