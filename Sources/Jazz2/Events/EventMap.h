#pragma once

#include "EventSpawner.h"
#include "../Direction.h"
#include "../GameDifficulty.h"
#include "../LevelDescriptor.h"
#include "../PitType.h"

#include "../../nCine/Base/BitArray.h"

#include <IO/Stream.h>
#include <algorithm>

using namespace Death::IO;

namespace Jazz2::Tiles
{
	class TileMap;
}

namespace Jazz2::Formats
{
	class LevelReader;
}

namespace Jazz2::Events
{
	/**
		@brief Represents event map, spawns triggered objects
		
		Owns the per-tile event layout of a level, together with spawn points, warp targets and active
		generators. Activates, deactivates and respawns actors as the level is played, supports rollback
		checkpoints, and can be serialized to and restored from a stream.

		Off-grid events --- objects placed at any position, not aligned to the tiles --- get a tile of their own
		in rows past the bottom of the event grid, one for each, so everything that refers to an event by its
		tile (an object deactivated or collected, a generator, a checkpoint, a snapshot) works for them too. Their
		object is spawned at their own position, and only queries by position in the level (@ref GetEventByPosition()
		and the like) never see them. Starts and warp targets among them keep their exact position. The events that
		act through the tile they're in rather than through an object (modifiers, areas, warp origins and the like,
		see @ref IsTileEffectEvent()) are applied to that tile, unless an event of the grid is there in any difficulty.
		Every off-grid event gets its tile before the events are filtered by the difficulty, so the tiles are the
		same in any difficulty, on a server and on its clients.
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
		/**
		 * @brief Rolls back to the last checkpoint
		 *
		 * @param respawnActors		Spawn again all objects that were active at the checkpoint but aren't anymore,
		 *							pass `false` if the objects are restored from a snapshot instead
		 */
		void RollbackToCheckpoint(bool respawnActors = true);

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
		/** @brief Marks event on specified tile position as active, i.e., its object already exists */
		void Activate(std::int32_t x, std::int32_t y);
		/** @brief Links an object restored from a snapshot to the generator on specified tile position */
		void AttachGeneratorActor(std::int32_t tx, std::int32_t ty, std::shared_ptr<Actors::ActorBase> actor);

		/** @brief Returns event description of specified tile position, or of the tile of an off-grid event */
		const EventTile& GetEventTile(std::int32_t x, std::int32_t y) const;
		/**
		 * @brief Returns the position in pixels the object of the event of a tile is spawned at
		 *
		 * The center of the tile, or the position of an off-grid event if the tile is one of theirs.
		 */
		Vector2i GetEventPosition(std::int32_t x, std::int32_t y) const;
		/**
		 * @brief Returns the tile of the event an object spawned from the event map at a position comes from
		 *
		 * The tile of an off-grid event of the type at exactly that position --- or of an off-grid generator of
		 * it --- otherwise the tile the position is in. The object being spawned from a tile right now gets that tile,
		 * only the first lookup that matches it does, an object of the same type it spawns there isn't from the tile.
		 */
		Vector2i GetOriginTile(Vector2i pos, EventType type);
		/** @brief Returns the number of off-grid events */
		std::int32_t GetOffGridEventCount() const {
			return (std::int32_t)_offGridPositions.size();
		}
		/** @brief Returns event type on specified position */
		EventType GetEventByPosition(float x, float y, std::uint8_t** eventParams);
		/** @overload */
		EventType GetEventByPosition(std::int32_t x, std::int32_t y, std::uint8_t** eventParams);
		/** @brief Returns `true` if specified tile position contains an event */
		bool HasEventByPosition(std::int32_t x, std::int32_t y) const;
		/**
		 * @brief Calls specified callback function for each event, until it returns `false`
		 *
		 * The events of the grid come row by row. With @p includeOffGrid the off-grid events come after them, with the
		 * coordinates of the tile their position is in, which can lie outside of the level (their own tile is
		 * past the grid, see @ref EventMap).
		 */
		void ForEachEvent(Function<bool(EventTile&, std::int32_t, std::int32_t)>&& forEachCallback, bool includeOffGrid = false) const;
		/**
		 * @brief Returns `true` if an event acts through the tile it's in rather than through an object
		 *
		 * Modifiers, areas, warp origins and the like, which are looked up by position in the level. An off-grid
		 * one is applied to the tile its position is in (see @ref EventMap).
		 */
		static bool IsTileEffectEvent(EventType type);
		/** @brief Returns `true` if specified position contains hurt event */
		bool IsHurting(float x, float y, Direction dir);
		/** @overload */
		bool IsHurting(std::int32_t x, std::int32_t y, Direction dir);
		/** @brief Returns ID of warp on specified position */
		std::int32_t GetWarpByPosition(float x, float y);
		/** @brief Returns target position for specified warp */
		Vector2f GetWarpTarget(std::uint32_t id);
		/** @brief Returns `true` if any target of specified warp lies within @p distance of @p pos */
		bool IsNearWarpTarget(std::uint32_t id, Vector2f pos, float distance) const;

		/**
		 * @brief Reads the events of a level
		 *
		 * Only the events present in the difficulty of @p options are added (and the multiplayer-only ones only in
		 * multiplayer). The tile modifiers among them are also applied to the sprite layer of @p tileMap.
		 */
		void LoadEvents(Formats::LevelReader& reader, Tiles::TileMap& tileMap, const LevelLoadOptions& options);
		/** @brief Adds target position for specified warp */
		void AddWarpTarget(std::uint16_t id, std::int32_t x, std::int32_t y);
		/** @brief Adds spawn position with specified player type mask */
		void AddSpawnPosition(std::uint8_t typeMask, std::int32_t x, std::int32_t y);

		/**
		 * @brief Initializes event map state from a stream
		 *
		 * Returns `false` if the state belongs to a layout of another size, such as of a level changed since, nothing
		 * is changed then and the rest of the state in the stream can't be read either.
		 */
		bool InitializeFromStream(Stream& src);
		/** @brief Serializes event map state to a stream */
		void SerializeResumableToStream(Stream& dest, bool fromCheckpoint = false);
		/**
		 * @brief Initializes active state of all events from a stream
		 *
		 * Returns `false` if the state belongs to a layout of another size, nothing is changed then.
		 */
		bool InitializeActiveStateFromStream(Stream& src);
		/** @brief Serializes active state of all events (the current one or at the last checkpoint) to a stream */
		void SerializeActiveStateToStream(Stream& dest, bool fromCheckpoint = false);
		/** @brief Restores complete live state of the event map (including the last checkpoint) from a snapshot */
		bool InitializeSnapshotFromStream(Stream& src);
		/** @brief Serializes complete live state of the event map (including the last checkpoint) to a snapshot */
		void SerializeSnapshotToStream(Stream& dest);

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
				_overflowed = false;
				_slots.reset(new (std::nothrow) std::uint16_t[count]);
				_count = (_slots != nullptr ? count : 0);
				if (_slots != nullptr) {
					std::memset(_slots.get(), 0, count * sizeof(std::uint16_t));
				}
				return (_slots != nullptr);
			}
			// Adds empty cells at the end, the tiles of the cells there already stay
			bool Grow(std::size_t count) {
				if (count <= _count) {
					return true;
				}
				std::unique_ptr<std::uint16_t[]> slots(new (std::nothrow) std::uint16_t[count]);
				if (slots == nullptr) {
					return false;
				}
				if (_count > 0) {
					std::memcpy(slots.get(), _slots.get(), _count * sizeof(std::uint16_t));
				}
				std::memset(slots.get() + _count, 0, (count - _count) * sizeof(std::uint16_t));
				_slots = Death::move(slots);
				_count = count;
				return true;
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
					if DEATH_UNLIKELY(_tiles.size() >= UINT16_MAX) {
						// A slot can't name another tile, so the event is written into a scratch tile, which nothing reads
						// back; a level that gets here is refused (see HasOverflowed())
						_overflowed = true;
						_overflowTile = {};
						return _overflowTile;
					}
					_tiles.push_back(EventTile {});
					_slots[i] = std::uint16_t(_tiles.size());
				}
				return _tiles[_slots[i] - 1];
			}
			// Returns `true` if more cells were given a tile than there can be
			bool HasOverflowed() const { return _overflowed; }

		private:
			std::unique_ptr<std::uint16_t[]> _slots;
			mutable SmallVector<EventTile, 0> _tiles;
			std::size_t _count = 0;
			EventTile _overflowTile = {};
			bool _overflowed = false;
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
		/// Positions of the off-grid events in pixels, the tile of the event `i` is `_layoutSize.X * _layoutSize.Y + i`
		SmallVector<Vector2i, 0> _offGridPositions;
		/// Indices of the off-grid events ordered by the row and then the column of the tile their position is in (and
		/// then by the index), so a part of the level finds its events without looking at all of them
		SmallVector<std::uint16_t, 0> _offGridOrder;
		/// First entry of @ref _offGridOrder of each row of tiles, one more entry marks the end of the last row
		SmallVector<std::int32_t, 0> _offGridRowStart;
		/// Rows of the layout, those of the grid and those past it that hold the off-grid events
		std::int32_t _layoutRows;
		/// The tile whose object is being spawned right now, its position and event type, the X is negative otherwise;
		/// an event of the grid and an off-grid one can share a position, the tile spawning tells them apart; cleared
		/// by the lookup of the spawned object, so another one it spawns at the same position doesn't take it
		Vector2i _spawningTile;
		Vector2i _spawningPos;
		EventType _spawningType;

		void SaveTileForRollback(std::uint32_t tileIndex, const EventTile& tile);
		/** @brief Returns the number of tiles of the layout, those of the off-grid events included */
		std::int32_t GetLayoutCount() const;
		/** @brief Returns `true` if a tile is in the layout, a tile of an off-grid event is too */
		bool IsInLayout(std::int32_t x, std::int32_t y) const;
		/** @brief Activates the event of a tile, spawns its object */
		void SpawnTileEvent(EventTile& tile, std::int32_t x, std::int32_t y, Actors::ActorState flags, std::int32_t z);
		/** @brief Reads the off-grid events, they're appended to the layout */
		void LoadOffGridEvents(Formats::LevelReader& reader, Tiles::TileMap& tileMap, const LevelLoadOptions& options, std::uint8_t difficultyFlag,
			BitArray& occupiedTiles);
		/** @brief Orders the off-grid events by the tile their position is in, see @ref _offGridOrder */
		void BuildOffGridIndex();
		/** @brief Returns the range of @ref _offGridOrder of the off-grid events in a row of tiles from a column on */
		std::pair<const std::uint16_t*, const std::uint16_t*> FindOffGridEvents(std::int32_t row, std::int32_t firstColumn) const;
		/** @brief Spawns the object of an event, remembering the tile it comes from for @ref GetOriginTile() */
		std::shared_ptr<Actors::ActorBase> SpawnFromTile(EventType type, std::uint8_t* params, Actors::ActorState flags, Vector2i tile, Vector2i pos, std::int32_t z);

		static void WriteEventTile(Stream& dest, const EventTile& tile);
		static void ReadEventTile(Stream& src, EventTile& tile);
	};
}