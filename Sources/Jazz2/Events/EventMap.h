#pragma once

#include "EventSpawner.h"
#include "../Direction.h"
#include "../GameDifficulty.h"
#include "../PitType.h"

#include "../../nCine/Base/BitArray.h"

#include <IO/Stream.h>
#include <algorithm>

using namespace Death::IO;

namespace Jazz2::Tiles
{
	class TileMap;
}

namespace Jazz2::Events
{
	/**
		@brief Represents event map, spawns triggered objects
		
		Owns the per-tile event layout of a level, together with spawn points, warp targets and active
		generators. Activates, deactivates and respawns actors as the level is played, supports rollback
		checkpoints, and can be serialized to and restored from a stream.
	*/
	class EventMap // : public IResumable
	{
	public:
		/** @brief Represents an event tile */
		struct EventTile {
			// One of these exists for every tile of the sprite layer, plus a full rollback copy of the grid, so
			// the fields are ordered widest first: the naturally-written order left the 16-bit event type padded
			// out to the alignment of the following flags and the trailing bool padded to the struct's own, which
			// cost four bytes per tile twice over.

			/** @brief Event flags */
			Actors::ActorState EventFlags;
			/** @brief Event type */
			EventType Event;
			/** @brief Whether the event is active */
			bool IsEventActive;
			/** @brief Event parameters */
			std::uint8_t EventParams[EventSpawner::SpawnParamsSize];
		};

		static_assert(sizeof(EventTile) == 24, "EventTile must stay packed, it is allocated per tile of the sprite layer");

		/** @brief Creates a new instance with the specified layout size */
		EventMap(Vector2i layoutSize);

		/** @brief Sets owner of the event map */
		void SetLevelHandler(ILevelHandler* levelHandler);
		/** @brief Whether reading the events ran out of memory (the level cannot be played) */
		bool HasLoadFailed() const { return _loadFailed; }
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
		/** @brief Calls specified callback function for each event */
		void ForEachEvent(Function<bool(EventTile&, std::int32_t, std::int32_t)>&& forEachCallback) const;
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
		void SerializeResumableToStream(Stream& dest, bool fromCheckpoint = false);

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

		// One event tile as it looked when the last checkpoint was taken. The checkpoint used to be a full copy
		// of the grid - a second 24-bytes-per-tile array as big as the sprite layer - although the only field
		// that changes as the player walks the level is IsEventActive, which is now a single bit per tile. The
		// rest of a tile only ever changes through StoreTileEvent() (an event consumed for good: a collected
		// item, an opened bird cage, a passed checkpoint), which saves the value it replaces here, once per
		// tile. The list is kept sorted by index so the rollback can merge it into its own walk of the grid.
		struct RollbackTile {
			std::uint32_t TileIndex;
			EventTile Tile;
		};
#endif

		ILevelHandler* _levelHandler;
		Vector2i _layoutSize;
		PitType _pitType;
		/**
			@brief The event layout: a 16-bit slot per tile, the tiles that hold an event in a dense array

			Almost every cell of a level's event grid is empty, yet the array of 24-byte tiles it used to be
			was the largest single block a level load made - 1.2 MB for the 768x64 Christmas levels, on a
			console with 5.8 MB of heap for everything. A cell now costs two bytes plus 24 for each actual
			event. Reads go through `operator[]` (an empty cell reads as a shared empty tile), writes through
			`Edit()`, which gives the cell a tile of its own; `Find()` tells the two apart without creating
			one. A reference from `Edit()` is valid only until the next one, because the dense array grows.
		*/
		class EventLayout {
		public:
			bool Allocate(std::size_t count) {
				_tiles.clear();
				_slots.reset(new (std::nothrow) std::uint16_t[count]);
				_count = (_slots != nullptr ? count : 0);
				if (_slots != nullptr) {
					std::memset(_slots.get(), 0, count * sizeof(std::uint16_t));
				}
				return (_slots != nullptr);
			}
			std::size_t size() const { return _count; }
			const EventTile& operator[](std::size_t i) const {
				static const EventTile empty = {};
				const std::uint16_t slot = _slots[i];
				return (slot != 0 ? _tiles[slot - 1] : empty);
			}
			// Like the array it replaces: a const layout still hands out mutable tiles
			EventTile* Find(std::size_t i) const {
				const std::uint16_t slot = _slots[i];
				return (slot != 0 ? &_tiles[slot - 1] : nullptr);
			}
			EventTile& Edit(std::size_t i) {
				if (_slots[i] == 0) {
					DEATH_ASSERT(_tiles.size() < UINT16_MAX, "Too many event tiles", _tiles.back());
					_tiles.push_back(EventTile {});
					_slots[i] = std::uint16_t(_tiles.size());
				}
				return _tiles[_slots[i] - 1];
			}

		private:
			std::unique_ptr<std::uint16_t[]> _slots;
			mutable SmallVector<EventTile, 0> _tiles;
			std::size_t _count = 0;
		};
		EventLayout _eventLayout;
		bool _loadFailed = false;
		SmallVector<RollbackTile, 0> _eventLayoutForRollback;
		BitArray _eventActiveForRollback;
		/// Whether a checkpoint was ever taken. Not implied by the two members above having contents --- a
		/// checkpoint starts out with an empty tile list, and a level may have no event tiles at all.
		bool _hasRollbackCheckpoint;
		SmallVector<GeneratorInfo, 0> _generators;
		SmallVector<SpawnPoint, 0> _spawnPoints;
		SmallVector<WarpTarget, 0> _warpTargets;

		void SaveTileForRollback(std::uint32_t tileIndex, const EventTile& tile);
	};
}