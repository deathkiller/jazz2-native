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
		/** @brief Returns the changes collected so far, the same tile may be there more than once until sealed */
		ArrayView<const Change> GetChanges() const {
			return _changes;
		}

		void Do(LevelDocument& document, DocumentChanges& changes) override;
		void Undo(LevelDocument& document, DocumentChanges& changes) override;
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

	private:
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
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

	private:
		const LevelDocument& _document;
		std::int32_t _width;
		std::int32_t _height;
		String _name;
		SmallVector<Change, 0> _changes;
		std::int32_t _minX, _minY, _maxX, _maxY;
		bool _sealed;
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
		 */
		static std::unique_ptr<SnapshotCommand> Begin(const LevelDocument& document, SnapshotParts parts, String name,
			DocumentChangeKind kinds = DocumentChangeKind::None);
		/** @brief Takes the snapshot after the change, the command can then be recorded */
		void End(const LevelDocument& document, DocumentChanges& changes);

		void Do(LevelDocument& document, DocumentChanges& changes) override;
		void Undo(LevelDocument& document, DocumentChanges& changes) override;
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

	private:
		String _name;
		DocumentChangeKind _kinds;
		DocumentSnapshot _before;
		DocumentSnapshot _after;

		SnapshotCommand(String name, DocumentChangeKind kinds);
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
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

	private:
		String _name;
		std::int32_t _layer;
		Formats::LayerRecord _oldRecord;
		Formats::LayerRecord _newRecord;
		String _oldName;
		String _newName;
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
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

	private:
		String _name;
		std::int32_t _index;
		DocumentAnimation _old;
		DocumentAnimation _new;
	};

	/**
		@brief Changes the events placed off the grid, all of them at once

		There are a few of them at most, so the whole list is kept before and after. Changes with the same name are
		merged when it's allowed, so dragging one is a single step.
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
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

	private:
		String _name;
		SmallVector<Formats::OffGridEventRecord, 0> _old;
		SmallVector<Formats::OffGridEventRecord, 0> _new;
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
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

	private:
		String _name;
		LevelFlags _oldFlags;
		LevelFlags _newFlags;
		Formats::LevelProperties _oldProperties;
		Formats::LevelProperties _newProperties;
	};

	/**
		@brief Changes how the level overrides the pixels and the collision mask of a tile

		Each of them is either set, or the override is removed so the tile is as its tile set has it. Changes of the
		same tile are merged when it's allowed, so a stroke of the pencil is a single step.
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
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

		/** @brief Finds the override of the pixels of a tile, or returns `nullptr` */
		static const Formats::TileDiffuseOverride* FindDiffuse(const LevelDocument& document, std::uint16_t tileId);
		/** @brief Finds the override of the collision mask of a tile, or returns `nullptr` */
		static const Formats::TileMaskOverride* FindMask(const LevelDocument& document, std::uint16_t tileId);

	private:
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

		Consecutive changes are merged, so dragging a color is a single step.
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
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

	private:
		String _name;
		bool _oldUse;
		bool _newUse;
		std::uint32_t _oldPalette[Formats::LevelFile::ColorsPerPalette];
		std::uint32_t _newPalette[Formats::LevelFile::ColorsPerPalette];

		void Apply(LevelDocument& document, DocumentChanges& changes, bool use, const std::uint32_t* palette);
	};

	/**
		@brief Changes the pixels or the masks of tiles the level stores in itself, see @ref Formats::ExtraTileSetRecord::Embedded

		The tiles stay as many and of the same kind, so the level shows the change at once without being loaded again.
		Consecutive changes are merged, so a stroke is a single step.
	*/
	class SetLevelTilesCommand : public EditCommand
	{
		DEATH_RUNTIME_OBJECT(EditCommand);

	public:
		/** @brief Creates a change of the extra tile set @p slot to the tile set file in @p data */
		SetLevelTilesCommand(const LevelDocument& document, std::int32_t slot, SmallVector<std::uint8_t, 0>&& data, String name);

		void Do(LevelDocument& document, DocumentChanges& changes) override;
		void Undo(LevelDocument& document, DocumentChanges& changes) override;
		bool TryMerge(EditCommand& next) override;
		String GetName() const override;
		std::size_t GetMemoryUsage() const override;

	private:
		String _name;
		std::int32_t _slot;
		SmallVector<std::uint8_t, 0> _oldData;
		SmallVector<std::uint8_t, 0> _newData;
	};
}

#endif
