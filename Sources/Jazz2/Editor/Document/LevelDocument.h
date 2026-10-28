#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Formats/LevelFile.h"
#include "../../../nCine/Primitives/Vector2.h"

#include <Containers/Function.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>
#include <IO/Stream.h>

using namespace nCine;

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::ArrayView;
	using Death::Containers::Function;
	using Death::Containers::SmallVector;
	using Death::Containers::SmallVectorImpl;
	using Death::Containers::String;
	using Death::Containers::StringView;
	using Death::IO::Stream;
#endif

	/**
		@brief Provides information about tile sets to a level being edited
	*/
	class ITileSetLibrary
	{
	public:
		virtual ~ITileSetLibrary() = default;

		/** @brief Returns the number of tiles of a tile set (a path relative to `Tilesets/`), or -1 if it doesn't exist */
		virtual std::int32_t GetTileCount(StringView name) = 0;
	};

	/**
		@brief Reference to a tile of a level being edited

		A level file refers to tiles by a single ID: the tiles of the default tile set first, those of the extra tile
		sets after them, and the animated tiles from the top of the ID space down. Adding a tile set or an animation
		moves these IDs, so the editor refers to a tile by where it comes from instead, and the IDs are worked out only
		when the level is saved.
	*/
	struct TileRef
	{
		/** @brief Source of a tile of the default tile set */
		static constexpr std::uint8_t DefaultTileSet = 0;
		/** @brief Last source of a tile of an extra tile set, those use @cpp 1 + index @ce of the tile set */
		static constexpr std::uint8_t LastExtraTileSet = 0xFD;
		/** @brief Source of a tile ID that doesn't refer to any known tile, it's kept as it is */
		static constexpr std::uint8_t Raw = 0xFE;
		/** @brief Source of an animated tile, @ref Index is the index of the animation */
		static constexpr std::uint8_t Animated = 0xFF;

		/** @brief Index of the tile within its source */
		std::uint16_t Index;
		/** @brief Where the tile comes from */
		std::uint8_t Source;
		/** @brief Flags as the file stores them (see @ref Formats::TileRecord) */
		std::uint8_t Flags;

		/** @brief Returns `true` if it's the empty tile */
		constexpr bool IsEmpty() const {
			return (Source == DefaultTileSet && Index == 0);
		}
		/** @brief Returns `true` if it's an animated tile */
		constexpr bool IsAnimated() const {
			return (Source == Animated);
		}

		constexpr bool operator==(const TileRef& other) const {
			return (Index == other.Index && Source == other.Source && Flags == other.Flags);
		}
		constexpr bool operator!=(const TileRef& other) const {
			return !operator==(other);
		}
	};

	/**
		@brief An animated tile of a level being edited
	*/
	struct DocumentAnimation
	{
		/** @brief Timing of the animation, its frames as the file stores them are not used */
		Formats::AnimatedTileRecord Timing;
		/** @brief Frames of the animation */
		SmallVector<TileRef, 0> Frames;
	};

	/** @brief Returns a new identity of a layer, unique while the editor runs, see @ref DocumentLayer::Id, it may be called on any thread */
	std::uint32_t MakeLayerId();

	/**
		@brief A layer of a level being edited
	*/
	struct DocumentLayer
	{
		/** @brief The editor hides the layer, the level itself isn't changed by it */
		static constexpr std::uint8_t EditorHidden = 0x01;
		/** @brief The editor doesn't let the layer be changed */
		static constexpr std::uint8_t EditorLocked = 0x02;

		/**
		 * @brief Description of the layer as the file stores it, including its size
		 *
		 * The renderer type and the color of the sprite layer are stored apart from its record, in
		 * @ref Formats::LayerPropertiesChunk, they're kept here as those of any other layer. So is everything else of the
		 * record the chunk stores (@ref Formats::LayerRecord::Textured, @ref Formats::LayerRecord::OriginalNumber,
		 * @ref Formats::LayerRecord::Blend).
		 */
		Formats::LayerRecord Record;
		/** @brief Name of the layer, may be empty */
		String Name;
		/** @brief Tiles of the layer, row by row */
		SmallVector<TileRef, 0> Tiles;
		/**
		 * @brief Texture of its own of a textured layer, taken instead of the 8×8 tiles at its top left, or empty
		 *
		 * Palette indices row by row, stored with the properties of the layer in @ref Formats::LayerPropertiesChunk (see
		 * @ref Formats::LayerPropertiesChunk::Entry::Texture), such as a texture of JJ2+ a converted level has.
		 */
		SmallVector<std::uint8_t, 0> Texture;
		/** @brief Combination of @ref EditorHidden and @ref EditorLocked, saved with the level but not part of it */
		std::uint8_t EditorFlags = 0;
		/** @brief How opaque the editor shows the layer, 255 fully, saved with the level but not part of it */
		std::uint8_t EditorOpacity = 255;
		/**
		 * @brief Identity of the layer while the editor runs, it's not saved
		 *
		 * A layer keeps it however it moves, so the editor state of a layer that is still there is kept when
		 * a snapshot of the layers is restored (see @ref DocumentSnapshot). The snapshots kept in a recovery copy
		 * give their layers new ones when they're read (see @ref LayerIdRemap).
		 */
		std::uint32_t Id = MakeLayerId();

		/** @brief Returns `true` if it's the sprite layer */
		bool IsSpriteLayer() const {
			return (Record.Kind == Formats::LayerKind::Sprite);
		}
		/** @brief Returns the width in tiles */
		std::int32_t GetWidth() const {
			return Record.Width;
		}
		/** @brief Returns the height in tiles */
		std::int32_t GetHeight() const {
			return Record.Height;
		}
		/** @brief Returns `true` if a position is inside of the layer */
		bool Contains(std::int32_t x, std::int32_t y) const {
			return (x >= 0 && y >= 0 && x < Record.Width && y < Record.Height);
		}
		/** @brief Returns the tile at a position, which has to be inside of the layer */
		const TileRef& GetTile(std::int32_t x, std::int32_t y) const {
			return Tiles[std::size_t(y) * std::size_t(Record.Width) + std::size_t(x)];
		}
		/** @copydoc GetTile() */
		TileRef& GetTile(std::int32_t x, std::int32_t y) {
			return Tiles[std::size_t(y) * std::size_t(Record.Width) + std::size_t(x)];
		}
	};

	/**
		@brief An event of the grid, at a tile of the sprite layer
	*/
	struct DocumentEvent
	{
		/** @brief Index of the tile, @cpp x + y * width @ce of the sprite layer */
		std::uint32_t Cell;
		/** @brief The event */
		Formats::EventRecord Record;
	};

	/** @brief Problem that would make a level unloadable, see @ref LevelDocument::Validate() */
	enum class DocumentProblem : std::uint8_t
	{
		None,					/**< No problem */
		LayerSize,				/**< A layer has a different number of tiles than its size */
		SpriteLayerCount,		/**< The level doesn't have exactly one sprite layer */
		TooManyLayers,			/**< The level has too many layers */
		TooManyTexts,			/**< The level has too many texts */
		TooManyExtraTileSets,	/**< The level has too many extra tile sets */
		TooManyAnimations,		/**< The level has too many animated tiles */
		TooManyFrames,			/**< An animated tile has too many frames */
		UnaddressableTile		/**< The level uses tiles past the tile IDs it can address */
	};

	/**
		@brief A level being edited

		Holds everything a level file stores, nothing is lost by loading a level and saving it again --- the file
		comes out byte for byte the same. It knows nothing of the engine, a level is played from the file it writes.
		Changes that should be undoable go through edit commands (see @ref CommandHistory), which tell what they
		changed by a @ref DocumentChange.

		Tiles are referred to by @ref TileRef, so adding a tile set or an animation doesn't touch any layer. Most
		tiles of the event grid are empty, only those with something in them are stored.

		Data that only the editor uses --- which layers are hidden, the editor's own chunks --- is saved with the
		level, but left out when the level is exported (see @ref Formats::LevelFile::IsEditorOnlyChunk()).
	*/
	class LevelDocument
	{
	public:
		/** @brief Size of the tile ID space of a new level */
		static constexpr std::int32_t DefaultTileIdSpace = 4096;
		/** @brief Most tile IDs the scripting API can address */
		static constexpr std::int32_t ScriptTileIdLimit = 4096;
		/**
		 * @brief Chunk with the editor state of the layers
		 *
		 * @cpp u8 @ce version (1), @cpp varuint @ce number of layers, @cpp varuint @ce bytes of each layer (at
		 * least 1), then the bytes of each layer in file order, the first being @ref DocumentLayer::EditorFlags, the
		 * second, if there are 2 at least, @ref DocumentLayer::EditorOpacity. A newer version may add bytes to each
		 * layer, a reader skips those it doesn't know.
		 */
		static constexpr std::uint32_t LayerStateChunk = Formats::LevelFile::MakeChunkTag('e', 'l', 'a', '2');
		/** @brief Chunk with the editor state of the layers written by 3.9.0, a byte for each layer, only read */
		static constexpr std::uint32_t LegacyLayerStateChunk = Formats::LevelFile::MakeChunkTag('e', 'l', 'a', 'y');
		/** @brief Level flags the events decide, the level is saved with them as @ref GetFlagsFromEvents() has them */
		static constexpr LevelFlags EventFlags = LevelFlags::HasLaps | LevelFlags::HasCaptureTheFlag | LevelFlags::HasMultiplayerSpawnPoints;

		/** @brief Level flags, those of @ref EventFlags as the level was loaded, they follow the events once it's saved */
		LevelFlags Flags;
		/** @brief Level properties */
		Formats::LevelProperties Properties;
		/** @brief Palette of the level, used only with @ref LevelFlags::UseLevelPalette */
		std::uint32_t LevelPalette[Formats::LevelFile::ColorsPerPalette];
		/** @brief Additional palettes */
		SmallVector<Formats::PaletteRecord, 0> AdditionalPalettes;
		/** @brief Number of tiles of the default tile set, or -1 if it isn't known */
		std::int32_t DefaultTileCount;
		/** @brief Extra tile sets, their tiles follow the tiles of the default tile set */
		SmallVector<Formats::ExtraTileSetRecord, 0> ExtraTileSets;
		/** @brief Tiles with a diffuse overridden by the level, by their tile ID */
		SmallVector<Formats::TileDiffuseOverride, 0> DiffuseOverrides;
		/** @brief Tiles with a collision mask overridden by the level, by their tile ID */
		SmallVector<Formats::TileMaskOverride, 0> MaskOverrides;
		/** @brief Level texts */
		SmallVector<String, 0> Texts;
		/** @brief Size of the tile ID space, the animated tiles take its top */
		std::int32_t TileIdSpace;
		/** @brief Animated tiles */
		SmallVector<DocumentAnimation, 0> Animations;
		/** @brief Layers in the order they are stored */
		SmallVector<DocumentLayer, 0> Layers;
		/** @brief Events of the grid that aren't empty, sorted by their tile */
		SmallVector<DocumentEvent, 0> Events;
		/** @brief Events placed anywhere, not aligned to the grid */
		SmallVector<Formats::OffGridEventRecord, 0> OffGridEvents;
		/** @brief Chunks, in the order they are stored, except those the document holds itself */
		SmallVector<Formats::ChunkRecord, 0> Chunks;

		/** @brief Creates an empty document, without any layer */
		LevelDocument();

		/** @brief Clears the document */
		void Clear();
		/**
		 * @brief Makes a new level of a given size with a tile set
		 *
		 * The level has a foreground layer, the sprite layer and two background layers, all empty.
		 */
		void CreateNew(StringView tileSet, std::int32_t tileCount, Vector2i size);

		/**
		 * @brief Loads a level from a stream
		 *
		 * The tile set library tells how many tiles the default tile set has, which is needed to tell the tiles of
		 * the extra tile sets apart. Without it, or if the tile set isn't found, those tiles are kept as raw IDs.
		 * Returns `false` if the stream doesn't hold a level in the game's own format or the level is damaged, the
		 * document is left empty then.
		 */
		bool Load(Stream& s, ITileSetLibrary* tileSets);
		/**
		 * @brief Saves the level to a stream
		 *
		 * With @p exportOnly the editor-only data is left out, as it should be in a level that is published.
		 * @p extraFlags are saved along with the flags of the level, such as @ref LevelFlags::NotEditable. Returns
		 * `false` if the level couldn't be written.
		 */
		bool Save(Stream& s, bool exportOnly = false, std::int32_t compressionLevel = 9, LevelFlags extraFlags = LevelFlags::None) const;

		/** @brief Returns the index of the sprite layer, or -1 if there is none */
		std::int32_t GetSpriteLayerIndex() const;
		/** @brief Returns the size of the event grid, which is the size of the sprite layer */
		Vector2i GetEventGridSize() const;

		/** @brief Returns the tile ID of the first animated tile, as the file stores it */
		std::int32_t GetAnimatedTilesOffset() const;
		/** @brief Returns the first tile ID of a source, or -1 if the source has no fixed range */
		std::int32_t GetSourceBaseId(std::uint8_t source) const;
		/** @brief Returns the number of tiles of a source, or -1 if it isn't known */
		std::int32_t GetSourceTileCount(std::uint8_t source) const;
		/** @brief Converts a tile reference to the tile ID the file stores */
		std::int32_t ToTileId(const TileRef& tile) const;
		/** @brief Converts a tile ID the file stores to a tile reference */
		TileRef FromTileId(std::int32_t tileId, std::uint8_t flags) const;

		/** @brief Makes an animated tile as the file stores it, with tile IDs instead of tile references */
		Formats::AnimatedTileRecord MakeAnimatedTileRecord(std::int32_t index) const;

		/** @brief Tiles overridden by the level as tile references, see @ref RemapTiles() */
		struct OverrideRefs
		{
			/** @brief Tile of each of @ref DiffuseOverrides */
			SmallVector<TileRef, 0> Diffuse;
			/** @brief Tile of each of @ref MaskOverrides */
			SmallVector<TileRef, 0> Mask;
		};

		/**
		 * @brief Returns the tiles overridden by the level as tile references
		 *
		 * The overrides are kept by tile ID, which moves with the tile sets before the tile. Taken before the tile sets
		 * change, they're put back by their new tile IDs by @ref RemapTiles().
		 */
		OverrideRefs CaptureOverrideRefs() const;
		/**
		 * @brief Changes the tile references of the whole level after its tile sets changed
		 *
		 * @p remap changes a reference of the layers and the animated tiles in place, or returns `false` if its tile is
		 * gone. A reference past the end of its tile set is gone too. A tile that is gone is replaced by the empty tile
		 * and its overrides are dropped. Returns the number of tiles of the layers that were replaced.
		 */
		std::int32_t RemapTiles(const OverrideRefs& overrides, Function<bool(TileRef&)>& remap);
		/** @brief What the tiles of a layer that grows are, see @ref ResizeLayer() */
		enum class ResizeFill : std::uint8_t
		{
			Empty,			/**< Empty tiles */
			ExtendEdges,	/**< The tiles of the edges go on, the corners fill the corners */
			Repeat			/**< The layer is repeated */
		};

		/**
		 * @brief Resizes a layer, its tiles move by an offset
		 *
		 * The tile (0, 0) of the layer as it was goes to @p offset, tiles that end up outside of the layer are gone.
		 * With the sprite layer, events and the water level move with the tiles, events that end up outside of the
		 * level are gone. Returns `false` if the size is too small or too large.
		 */
		bool ResizeLayer(std::int32_t layer, Vector2i size, Vector2i offset, ResizeFill fill);

		/** @brief Returns the number of tiles of the default tile set and all the extra tile sets, or -1 if it isn't known */
		std::int32_t GetStaticTileCount() const;
		/** @brief Makes the tile ID space larger if the tiles of the tile sets would reach the animated tiles */
		void FitTileIdSpace();
		/**
		 * @brief Returns the flags of @ref EventFlags the events of the level decide
		 *
		 * As the converter of the original levels decides them: a race with laps has a warp with "Set Lap", Capture
		 * the Flag has a base, both only in a level for multiplayer, and a multiplayer start makes multiplayer spawn
		 * points. Events a generator makes don't count.
		 */
		LevelFlags GetFlagsFromEvents() const;
		/** @brief Returns the flags the level is saved with, @ref Flags with those the events decide */
		LevelFlags GetSavedFlags() const;
		/**
		 * @brief Returns `true` if the level can save a tile by its ID
		 *
		 * A tile of the tile sets at or past the first animated tile can't be, its ID would be read as an animated tile
		 * or wouldn't fit (see @ref GetAnimatedTilesOffset()), the space has to grow first. The empty tile, animated
		 * tiles and raw tile IDs always can be.
		 */
		bool IsTileAddressable(const TileRef& tile) const;

		/** @brief Returns the event at a tile of the grid, an empty one if there is none */
		const Formats::EventRecord& GetEvent(std::int32_t x, std::int32_t y) const;
		/** @brief Sets the event at a tile of the grid, an empty event removes it */
		void SetEvent(std::int32_t x, std::int32_t y, const Formats::EventRecord& event);
		/**
		 * @brief Sets events at many tiles of the grid at once, an empty event removes the one there
		 *
		 * @p events have to be sorted by their tile, each tile at most once. They're merged into @ref Events in a single
		 * pass, which is much faster than setting them one by one when there are many.
		 */
		void SetEvents(ArrayView<const DocumentEvent> events);
		/** @brief Returns the position of the event of a tile in @ref Events, or where it would be inserted */
		std::size_t FindEvent(std::uint32_t cell) const;

		/** @brief Returns the empty event, as stored for a tile without any event */
		static const Formats::EventRecord& GetEmptyEvent();
		/** @brief Returns `true` if an event is the empty one, which doesn't have to be stored */
		static bool IsEmptyEvent(const Formats::EventRecord& event);
		/** @brief Returns `true` if two events are written the same */
		static bool AreEventsEqual(const Formats::EventRecord& a, const Formats::EventRecord& b);
		/** @brief Brings an event to the form the document keeps, without the bits the writer derives */
		static void NormalizeEvent(Formats::EventRecord& event);

		/** @brief Returns the chunk with a tag, or `nullptr` */
		const Formats::ChunkRecord* FindChunk(std::uint32_t tag) const;
		/** @brief Sets the payload of a chunk, the chunk is added if it doesn't exist, and removed if the payload is empty */
		void SetChunk(std::uint32_t tag, ArrayView<const std::uint8_t> data);
		/** @brief Sets the payload of a chunk as @ref SetChunk() does, taking the data instead of copying it */
		void ReplaceChunk(std::uint32_t tag, SmallVector<std::uint8_t, 0>&& data);

		/**
		 * @brief Checks the document for problems that would make the level unloadable
		 *
		 * Returns `false` and the first problem found if there is any, the editor shows it translated, see
		 * @ref GetProblemDescription() for the text of a log.
		 */
		bool Validate(DocumentProblem* problem = nullptr) const;

		/** @brief Returns a problem of @ref Validate() as a text of a log, it isn't translated */
		static StringView GetProblemDescription(DocumentProblem problem);

	private:
		bool LoadLayerStates(const Formats::ChunkRecord& chunk);
		void LoadLegacyLayerStates(const Formats::ChunkRecord& chunk);
		void WriteLayerStates(SmallVectorImpl<std::uint8_t>& data) const;
	};
}

#endif
