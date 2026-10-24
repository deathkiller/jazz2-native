#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "CommandHistory.h"
#include "DocumentSnapshot.h"

namespace Jazz2::Editor
{
	/**
		@brief Changes tiles of a layer

		The tiles are collected first --- the document isn't touched until the command is executed --- so a tool
		can show them while they are being drawn. The tile the document has when a tile is first set is the one
		restored by undo.
	*/
	class SetTilesCommand : public EditCommand
	{
		DEATH_RUNTIME_OBJECT(EditCommand);

	public:
		/** @brief Change of a single tile */
		struct Change
		{
			/** @brief Index of the tile, @cpp x + y * width @ce */
			std::uint32_t Cell;
			/** @brief Tile before the change */
			TileRef Old;
			/** @brief Tile after the change */
			TileRef New;
		};

		/** @brief Creates an empty change of a layer */
		SetTilesCommand(const LevelDocument& document, std::int32_t layer, String name);

		/** @brief Sets a tile, positions outside of the layer are ignored */
		void Set(std::int32_t x, std::int32_t y, const TileRef& tile);
		/**
		 * @brief Completes the collection
		 *
		 * Tiles set more than once are merged, tiles that end up unchanged are dropped. It's called by
		 * @ref Do() if it wasn't before.
		 */
		void Seal();

		/** @brief Returns `true` if there's nothing to change */
		bool IsEmpty() const;
		/** @brief Returns the layer */
		std::int32_t GetLayer() const {
			return _layer;
		}
		/** @brief Returns the area of all changed tiles */
		Recti GetArea() const;
		/**
		 * @brief Returns the changes collected so far, the same tile may be there more than once until sealed
		 *
		 * A sealed command of many changes keeps them compressed, it returns none then.
		 */
		ArrayView<const Change> GetChanges() const {
			return _changes;
		}

		void Do(LevelDocument& document, DocumentChanges& changes) override;
		void Undo(LevelDocument& document, DocumentChanges& changes) override;
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

	private:
		friend class CommandArchive;

		// Changes of more tiles than this are kept compressed
		static constexpr std::size_t CompressThreshold = 2048;

		const LevelDocument& _document;
		std::int32_t _layer;
		std::int32_t _width;
		String _name;
		SmallVector<Change, 0> _changes;
		CompressedBuffer _compressed;
		std::int32_t _minX, _minY, _maxX, _maxY;
		bool _sealed;

		void Apply(LevelDocument& document, DocumentChanges& changes, bool undo);
	};

	/**
		@brief Changes events of the grid

		Collected the same way as by @ref SetTilesCommand.
	*/
	class SetEventsCommand : public EditCommand
	{
		DEATH_RUNTIME_OBJECT(EditCommand);

	public:
		/** @brief Change of a single event */
		struct Change
		{
			/** @brief Index of the tile, @cpp x + y * width @ce */
			std::uint32_t Cell;
			/** @brief Event before the change */
			Formats::EventRecord Old;
			/** @brief Event after the change */
			Formats::EventRecord New;
		};

		/** @brief Creates an empty change of events */
		SetEventsCommand(const LevelDocument& document, String name);

		/** @brief Sets an event, positions outside of the grid are ignored */
		void Set(std::int32_t x, std::int32_t y, const Formats::EventRecord& event);
		/** @brief Completes the collection, see @ref SetTilesCommand::Seal() */
		void Seal();

		/** @brief Returns `true` if there's nothing to change */
		bool IsEmpty() const;
		/** @brief Returns the area of all changed events */
		Recti GetArea() const;
		/** @brief Returns the changes collected so far */
		ArrayView<const Change> GetChanges() const {
			return _changes;
		}

		void Do(LevelDocument& document, DocumentChanges& changes) override;
		void Undo(LevelDocument& document, DocumentChanges& changes) override;
		/** @brief Merges a change of the same events by the same name, as made while a value is dragged */
		bool TryMerge(EditCommand& next) override;
		bool CanMerge(EditCommand& next) override;
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

	private:
		friend class CommandArchive;

		const LevelDocument& _document;
		std::int32_t _width;
		std::int32_t _height;
		String _name;
		SmallVector<Change, 0> _changes;
		std::int32_t _minX, _minY, _maxX, _maxY;
		bool _sealed;

		void Apply(LevelDocument& document, bool undo);
	};

	/**
		@brief Change of parts of a level kept as snapshots before and after it

		Used for changes too large or too varied for a dedicated command, such as adding or resizing a layer.
		The change is made directly on the document between @ref Begin() and @ref End().
	*/
	class SnapshotCommand : public EditCommand
	{
		DEATH_RUNTIME_OBJECT(EditCommand);

	public:
		/**
		 * @brief Takes the snapshot before a change
		 *
		 * The change is reported as changing all that the parts hold, unless @p kinds says what it really changes ---
		 * a change of tile references only doesn't have to load the shown level again as a change of layers would.
		 * With @p layer, only that layer of @ref SnapshotParts::Layers is kept, for a change of a single layer that
		 * doesn't add or remove any.
		 */
		static std::unique_ptr<SnapshotCommand> Begin(const LevelDocument& document, SnapshotParts parts, String name,
			DocumentChangeKind kinds = DocumentChangeKind::None, std::int32_t layer = -1);
		/**
		 * @brief Takes the snapshot after the change, the command can then be recorded
		 *
		 * The document already is as the change made it, so the command is recorded by @ref CommandHistory::Record()
		 * rather than executed, which would only put the same state back.
		 */
		void End(const LevelDocument& document, DocumentChanges& changes);

		void Do(LevelDocument& document, DocumentChanges& changes) override;
		void Undo(LevelDocument& document, DocumentChanges& changes) override;
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

	private:
		friend class CommandArchive;

		String _name;
		DocumentChangeKind _kinds;
		DocumentSnapshot _before;
		DocumentSnapshot _after;

		SnapshotCommand(String name, DocumentChangeKind kinds);
	};

	/**
		@brief Changes the payload of a chunk, as an undoable step

		For data the level keeps in chunks of its own, such as bookmarks and notes. An empty payload removes the
		chunk. Changes of the same chunk by the same name are merged when it's allowed.
	*/
	class SetChunkCommand : public EditCommand
	{
		DEATH_RUNTIME_OBJECT(EditCommand);

	public:
		/** @brief Creates a change from the current payload of the chunk to the given one */
		SetChunkCommand(const LevelDocument& document, std::uint32_t tag, ArrayView<const std::uint8_t> data, String name);

		void Do(LevelDocument& document, DocumentChanges& changes) override;
		void Undo(LevelDocument& document, DocumentChanges& changes) override;
		bool TryMerge(EditCommand& next) override;
		bool CanMerge(EditCommand& next) override;
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

	private:
		friend class CommandArchive;

		String _name;
		std::uint32_t _tag;
		SmallVector<std::uint8_t, 0> _oldData;
		SmallVector<std::uint8_t, 0> _newData;
	};

	/**
		@brief Moves a layer to another position in the list of layers

		Only the order changes, so nothing has to be kept but the two positions. The ground lines follow their layers.
	*/
	class MoveLayerCommand : public EditCommand
	{
		DEATH_RUNTIME_OBJECT(EditCommand);

	public:
		/** @brief Creates a move of the layer @p from to the position @p to */
		MoveLayerCommand(std::int32_t from, std::int32_t to, String name);

		void Do(LevelDocument& document, DocumentChanges& changes) override;
		void Undo(LevelDocument& document, DocumentChanges& changes) override;
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

	private:
		friend class CommandArchive;

		String _name;
		std::int32_t _from;
		std::int32_t _to;

		static void Move(LevelDocument& document, std::int32_t from, std::int32_t to, DocumentChanges& changes);
	};

	/**
		@brief Adds a layer or removes one

		Only the layer itself is kept, compressed while it isn't in the level, not the whole level. The ground lines
		follow the layers after it, those of a removed layer come back with it.
	*/
	class InsertLayerCommand : public EditCommand
	{
		DEATH_RUNTIME_OBJECT(EditCommand);

	public:
		/** @brief Creates an addition of a layer at @p index */
		static std::unique_ptr<InsertLayerCommand> Add(const LevelDocument& document, std::int32_t index, const DocumentLayer& layer, String name);
		/** @brief Creates a removal of the layer at @p index */
		static std::unique_ptr<InsertLayerCommand> Remove(const LevelDocument& document, std::int32_t index, String name);

		void Do(LevelDocument& document, DocumentChanges& changes) override;
		void Undo(LevelDocument& document, DocumentChanges& changes) override;
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

	private:
		friend class CommandArchive;

		String _name;
		std::int32_t _index;
		bool _insert;
		// The layer while it isn't in the level
		CompressedBuffer _layer;
		// Ground lines before and after the change, the latter known once it was done
		SmallVector<std::uint8_t, 0> _oldSplines;
		SmallVector<std::uint8_t, 0> _newSplines;
		bool _hasNewSplines;

		InsertLayerCommand(String name, std::int32_t index, bool insert);

		// Return false if the layer can't be put back or removed, the level stays as it is then
		bool Insert(LevelDocument& document, DocumentChanges& changes);
		bool Erase(LevelDocument& document, DocumentChanges& changes);
	};

	/**
		@brief Changes the description or the name of a layer, its size stays the same
	*/
	class SetLayerPropertiesCommand : public EditCommand
	{
		DEATH_RUNTIME_OBJECT(EditCommand);

	public:
		/** @brief Creates a change from the current state of the layer to the given one */
		SetLayerPropertiesCommand(const LevelDocument& document, std::int32_t layer, const Formats::LayerRecord& record,
			StringView layerName, String name);

		void Do(LevelDocument& document, DocumentChanges& changes) override;
		void Undo(LevelDocument& document, DocumentChanges& changes) override;
		bool TryMerge(EditCommand& next) override;
		bool CanMerge(EditCommand& next) override;
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

		/**
		 * @brief Lets a change of other fields of the layer merge into it, if that one lets it too
		 *
		 * As moving a layer by its frame changes either of its offsets or both as it's dragged, but is one step. Without
		 * it, only a change of the same fields merges, as a value dragged in the properties.
		 */
		void SetMergesAnyField(bool value) {
			_mergesAnyField = value;
		}

	private:
		friend class CommandArchive;

		String _name;
		std::int32_t _layer;
		Formats::LayerRecord _oldRecord;
		Formats::LayerRecord _newRecord;
		String _oldName;
		String _newName;
		// What the change changes, only a change of the same is merged into it, unless _mergesAnyField
		std::uint32_t _changedFields;
		// See SetMergesAnyField(), only while it's being made, it isn't archived
		bool _mergesAnyField = false;

		static std::uint32_t GetChangedFields(const Formats::LayerRecord& a, const Formats::LayerRecord& b, StringView nameA, StringView nameB);
		// Puts a record into a layer, its size stays that of the tiles
		static void SetRecord(DocumentLayer& layer, const Formats::LayerRecord& record);
	};

	/**
		@brief Replaces the texture of its own of a layer, or removes it, see @ref DocumentLayer::Texture
	*/
	class SetLayerTextureCommand : public EditCommand
	{
		DEATH_RUNTIME_OBJECT(EditCommand);

	public:
		/** @brief Creates a change from the current texture of the layer to @p texture, empty to take its tiles again */
		SetLayerTextureCommand(const LevelDocument& document, std::int32_t layer, SmallVector<std::uint8_t, 0> texture, String name);

		void Do(LevelDocument& document, DocumentChanges& changes) override;
		void Undo(LevelDocument& document, DocumentChanges& changes) override;
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

	private:
		friend class CommandArchive;

		String _name;
		std::int32_t _layer;
		SmallVector<std::uint8_t, 0> _old;
		SmallVector<std::uint8_t, 0> _new;
	};

	/**
		@brief Changes the timing and the frames of an animated tile, the number of animated tiles stays the same
	*/
	class SetAnimationCommand : public EditCommand
	{
		DEATH_RUNTIME_OBJECT(EditCommand);

	public:
		/** @brief Creates a change from the current state of the animated tile to the given one */
		SetAnimationCommand(const LevelDocument& document, std::int32_t index, DocumentAnimation animation, String name);

		void Do(LevelDocument& document, DocumentChanges& changes) override;
		void Undo(LevelDocument& document, DocumentChanges& changes) override;
		/** @brief Merges a change of the same animated tile by the same name, as made while a value is dragged */
		bool TryMerge(EditCommand& next) override;
		bool CanMerge(EditCommand& next) override;
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

	private:
		friend class CommandArchive;

		String _name;
		std::int32_t _index;
		DocumentAnimation _old;
		DocumentAnimation _new;
		// What the change changes, only a change of the same is merged into it
		std::uint32_t _changedFields;

		static std::uint32_t GetChangedFields(const DocumentAnimation& a, const DocumentAnimation& b);
	};

	/**
		@brief Changes the events placed off the grid, all of them at once

		Given the whole list as it is to be, only the parts of it that change are kept, before and after, so a step
		of a drag over a few of many events doesn't keep all of them. Changes with the same name of the same events
		are merged when it's allowed, so dragging a value of one or more of them is a single step.
	*/
	class SetOffGridEventsCommand : public EditCommand
	{
		DEATH_RUNTIME_OBJECT(EditCommand);

	public:
		/** @brief Creates a change from the current events off the grid to the given ones */
		SetOffGridEventsCommand(const LevelDocument& document, SmallVector<Formats::OffGridEventRecord, 0> events, String name);

		void Do(LevelDocument& document, DocumentChanges& changes) override;
		void Undo(LevelDocument& document, DocumentChanges& changes) override;
		bool TryMerge(EditCommand& next) override;
		bool CanMerge(EditCommand& next) override;
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

	private:
		friend class CommandArchive;

		// A run of events replaced by another one, at an index of the list before the change; the records of all runs
		// follow each other in _oldRecords and _newRecords
		struct Hunk
		{
			std::uint32_t Index;
			std::uint32_t OldCount;
			std::uint32_t NewCount;
		};

		String _name;
		// Sizes of the whole list before and after the change
		std::uint32_t _oldCount;
		std::uint32_t _newCount;
		// Sorted by the index, they don't overlap
		SmallVector<Hunk, 0> _hunks;
		SmallVector<Formats::OffGridEventRecord, 0> _oldRecords;
		SmallVector<Formats::OffGridEventRecord, 0> _newRecords;
		// Indices of the events the change changes, sorted, only a change of the same is merged into it
		SmallVector<std::uint32_t, 0> _changedEvents;
		// Whether the change adds or removes events, which is never merged
		bool _structural;

		void FindChangedEvents();
		// Returns false if the runs don't fit the counts, as a damaged recovery copy could have them
		bool AreHunksValid() const;
		void Apply(LevelDocument& document, DocumentChanges& changes, bool undo);
	};

	/**
		@brief Changes the level flags and properties
	*/
	class SetLevelPropertiesCommand : public EditCommand
	{
		DEATH_RUNTIME_OBJECT(EditCommand);

	public:
		/** @brief Creates a change from the current properties to the given ones */
		SetLevelPropertiesCommand(const LevelDocument& document, LevelFlags flags, const Formats::LevelProperties& properties, String name);

		void Do(LevelDocument& document, DocumentChanges& changes) override;
		void Undo(LevelDocument& document, DocumentChanges& changes) override;
		bool TryMerge(EditCommand& next) override;
		bool CanMerge(EditCommand& next) override;
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

	private:
		friend class CommandArchive;

		String _name;
		LevelFlags _oldFlags;
		LevelFlags _newFlags;
		Formats::LevelProperties _oldProperties;
		Formats::LevelProperties _newProperties;
		// What the change changes, the flag bits and a bit for each property, only a change of the same is merged into it
		std::uint32_t _changedFields;

		static std::uint32_t GetChangedFields(LevelFlags flagsA, LevelFlags flagsB, const Formats::LevelProperties& a, const Formats::LevelProperties& b);
	};

	/**
		@brief Changes how the level overrides the pixels and the collision mask of a tile

		Each of them is either set, or the override is removed so the tile is as its tile set has it. Changes of the
		same tile by the same name are merged when it's allowed, so a stroke of the pencil is a single step.
	*/
	class SetTileOverrideCommand : public EditCommand
	{
		DEATH_RUNTIME_OBJECT(EditCommand);

	public:
		/** @brief Creates a change of a tile, a `nullptr` removes the override of its pixels or of its mask */
		SetTileOverrideCommand(const LevelDocument& document, std::uint16_t tileId, const std::uint8_t* indices, const std::uint8_t* mask, String name);

		void Do(LevelDocument& document, DocumentChanges& changes) override;
		void Undo(LevelDocument& document, DocumentChanges& changes) override;
		bool TryMerge(EditCommand& next) override;
		bool CanMerge(EditCommand& next) override;
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

		/** @brief Returns the tile ID of the tile */
		std::uint16_t GetTileId() const {
			return _tileId;
		}
		/** @brief Returns `true` if the tile is overridden after the change, its pixels or its mask */
		bool IsOverriddenAfter() const {
			return (_new.HasDiffuse || _new.HasMask);
		}

		/** @brief Finds the override of the pixels of a tile, or returns `nullptr` */
		static const Formats::TileDiffuseOverride* FindDiffuse(const LevelDocument& document, std::uint16_t tileId);
		/** @brief Finds the override of the collision mask of a tile, or returns `nullptr` */
		static const Formats::TileMaskOverride* FindMask(const LevelDocument& document, std::uint16_t tileId);

	private:
		friend class CommandArchive;

		static constexpr std::int32_t PixelCount = Formats::LevelFile::TileSize * Formats::LevelFile::TileSize;

		struct State
		{
			bool HasDiffuse = false;
			bool HasMask = false;
			std::uint8_t Indices[PixelCount];
			std::uint8_t Mask[PixelCount];
		};

		String _name;
		std::uint16_t _tileId;
		State _old;
		State _new;

		void Apply(LevelDocument& document, DocumentChanges& changes, const State& state);
	};

	/**
		@brief Changes the palette of the level, and whether the level uses it

		Consecutive changes of the same entries by the same name are merged, so dragging a color is a single step.
	*/
	class SetLevelPaletteCommand : public EditCommand
	{
		DEATH_RUNTIME_OBJECT(EditCommand);

	public:
		/** @brief Creates a change from the current palette to the given one */
		SetLevelPaletteCommand(const LevelDocument& document, bool usePalette, const std::uint32_t* palette, String name);

		void Do(LevelDocument& document, DocumentChanges& changes) override;
		void Undo(LevelDocument& document, DocumentChanges& changes) override;
		bool TryMerge(EditCommand& next) override;
		bool CanMerge(EditCommand& next) override;
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

	private:
		friend class CommandArchive;

		String _name;
		bool _oldUse;
		bool _newUse;
		std::uint32_t _oldPalette[Formats::LevelFile::ColorsPerPalette];
		std::uint32_t _newPalette[Formats::LevelFile::ColorsPerPalette];
		// A bit for each entry the change changes, only a change of the same entries is merged into it
		std::uint32_t _changedEntries[Formats::LevelFile::ColorsPerPalette / 32];

		void Apply(LevelDocument& document, DocumentChanges& changes, bool use, const std::uint32_t* palette);
	};

	/**
		@brief Changes the pixels or the masks of tiles the level stores in itself, see @ref Formats::ExtraTileSetRecord::Embedded

		The tiles stay as many and of the same kind, so the level shows the change at once without being loaded again.
		Consecutive changes are merged, so a stroke is a single step. Only a tile set the level stores in itself can be
		changed, a command made for any other slot does nothing. The data is swapped with the document instead of
		copied, so a step keeps a single copy of the tile set.
	*/
	class SetLevelTilesCommand : public EditCommand
	{
		DEATH_RUNTIME_OBJECT(EditCommand);

	public:
		/**
		 * @brief Creates a change of the extra tile set @p slot to the tile set file in @p data
		 *
		 * With @p flags other than -1, the flags of the tile set record change too, as when the colors of its tiles
		 * are matched to the palette no longer (see @ref Formats::ExtraTileSetRecord::Remapped). It stays embedded.
		 */
		SetLevelTilesCommand(const LevelDocument& document, std::int32_t slot, SmallVector<std::uint8_t, 0>&& data, String name, std::int32_t flags = -1);

		void Do(LevelDocument& document, DocumentChanges& changes) override;
		void Undo(LevelDocument& document, DocumentChanges& changes) override;
		bool TryMerge(EditCommand& next) override;
		bool CanMerge(EditCommand& next) override;
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

	private:
		friend class CommandArchive;

		String _name;
		std::int32_t _slot;
		// The data the document doesn't have at the moment, the new one before it's done, the old one after
		SmallVector<std::uint8_t, 0> _data;
		// The flags the record doesn't have at the moment, -1 if they don't change
		std::int32_t _flags;

		void Swap(LevelDocument& document, DocumentChanges& changes);
	};

	/**
	 * @brief Executes a change, the tile ID space of the level grows in the same step if the change uses a tile past it
	 *
	 * A tile of the tile sets the level can't address yet (see @ref LevelDocument::IsTileAddressable()) can be placed
	 * this way, or overridden by @ref SetTileOverrideCommand, the space is made large enough for all tiles of the tile
	 * sets then, so it doesn't grow again with the next one. Only the animated tiles move by that --- they're referred to by their index everywhere but in the
	 * file, scripts included --- so the level stays as it was otherwise. The change and the growth are undone at once,
	 * they're a transaction of their own if there's none in progress.
	 */
	void ExecuteGrowingTileIdSpace(CommandHistory& history, std::unique_ptr<EditCommand> command, DocumentChanges& changes, bool allowMerge = false);
}

#endif
