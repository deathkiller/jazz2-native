#pragma once

#include "../../Main.h"
#include "../ContentFileTypes.h"
#include "../LevelFlags.h"

#include <Containers/ArrayView.h>
#include <Containers/Function.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>
#include <IO/MemoryStream.h>
#include <IO/Stream.h>
#include <IO/Compression/DeflateStream.h>

#include <memory>

namespace Jazz2::Formats
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::ArrayView;
	using Death::Containers::Function;
	using Death::Containers::SmallVector;
	using Death::Containers::SmallVectorImpl;
	using Death::Containers::String;
	using Death::Containers::StringView;
	using Death::IO::MemoryStream;
	using Death::IO::Stream;
#endif

	/**
		@brief Uncompressed header of a level file

		Read by @ref LevelFile::ReadHeader(), which leaves the stream at the start of the deflated body that
		a @ref LevelReader then reads.
	*/
	struct LevelHeader
	{
		/** @brief Level flags */
		LevelFlags Flags;
		/** @brief Version of the layout */
		std::uint16_t Version;
		/** @brief Size of the deflated body that follows the header */
		std::int32_t BodySize;
	};

	/**
		@brief Layout of the game's own level format

		A level is converted once from the original `.j2l` (see @ref Compatibility::JJ2Level::Convert()), or saved
		by the level editor, and then read by the game in this form. Everything is read and written through
		@ref LevelReader and @ref LevelWriter, which are the only places that know the byte layout.

		The header starts with the same signature and type/version fields every other file the game writes uses.
		Only @ref CurrentVersion is read --- a level of another version is refused rather than misread, and the
		converted levels in the cache are rebuilt whenever the version changes (see
		@ref Compatibility::JJ2Anims::CacheVersion).

		@par File layout
		All values are little-endian. A @cpp str8 @ce is a @cpp u8 @ce length followed by that many UTF-8 bytes,
		a @cpp varuint @ce is an unsigned LEB128 value.
		- @cpp u64 @ce signature, @cpp u8 @ce file type, @cpp u16 @ce version, @cpp u16 @ce @ref LevelFlags
		- @cpp i32 @ce size of the raw-deflated body that follows, everything below being inside it:
		- @ref LevelProperties
		- the level palette (256 × @cpp u32 @ce, only with @ref LevelFlags::UseLevelPalette), then the additional
		  palettes (@cpp u8 @ce count × @ref PaletteRecord)
		- extra tile sets (@cpp u8 @ce count × @ref ExtraTileSetRecord)
		- overridden tile diffuse and masks (@cpp varuint @ce count × @ref TileDiffuseOverride, then the same for
		  @ref TileMaskOverride)
		- level texts (@cpp u8 @ce count × @cpp u16 @ce length + UTF-8)
		- @cpp u16 @ce first animated tile ID, then @cpp u16 @ce count × @ref AnimatedTileRecord
		- @cpp u8 @ce layer count × (@ref LayerRecord + width × height × @ref TileRecord)
		- the event grid, one @ref EventRecord for each tile of the sprite layer, row by row
		- @cpp varuint @ce count × @ref OffGridEventRecord
		- optionally, @cpp varuint @ce count × @ref ChunkRecord --- a body that ends before it has no chunks

		@par Chunks
		Chunks carry anything the fixed sections above don't, tagged by a four-character code. A reader skips
		the chunks it doesn't know, so new ones can be added without changing the version. A tag whose first
		letter is lowercase is editor-only data (see @ref IsEditorOnlyChunk()), dropped when a level is exported.

		@par Version history
		- 1 --- first versioned layout; a layer may use @ref Tiles::LayerRendererType::XRay and
		  @ref Tiles::LayerRendererType::BlurBehind; optional chunks at the end of the body; @ref LevelProperties::Author
		  after the display name
	*/
	struct LevelFile
	{
		/** @brief Signature shared by all files the game writes */
		static constexpr std::uint64_t Signature = 0x2095A59FF0BFBBEF;
		/** @brief Version this build writes, and the only one it reads */
		static constexpr std::uint16_t CurrentVersion = 1;
		/** @brief Size of the parameters of an event */
		static constexpr std::int32_t EventParamsSize = 16;
		/** @brief Number of colors in a palette */
		static constexpr std::int32_t ColorsPerPalette = 256;
		/** @brief Size of a tile in pixels */
		static constexpr std::int32_t TileSize = 32;

		/** @brief Layer names (one @cpp str8 @ce per layer, in file order), only the editor uses them */
		static constexpr std::uint32_t LayerNamesChunk = 0x6D616E6C;		// "lnam"
		/**
		 * @brief Small image of the level shown in the list of custom levels, kept when the level is exported
		 *
		 * @cpp u16 @ce width and height, then @cpp R, G, B @ce bytes for each pixel, row by row. Written by the level
		 * editor whenever it saves a level, a pixel for one or more tiles of the sprite layer.
		 */
		static constexpr std::uint32_t PreviewChunk = 0x57565250;			// "PRVW"

		/**
		 * @brief Reads the uncompressed header of a level
		 *
		 * Leaves the stream at the start of the deflated body. Returns `false` if the stream doesn't hold a level
		 * of @ref CurrentVersion.
		 */
		static bool ReadHeader(Stream& s, LevelHeader& header);

		/** @brief Makes a chunk tag from its four characters */
		static constexpr std::uint32_t MakeChunkTag(char a, char b, char c, char d) {
			return std::uint32_t(std::uint8_t(a)) | (std::uint32_t(std::uint8_t(b)) << 8) |
				(std::uint32_t(std::uint8_t(c)) << 16) | (std::uint32_t(std::uint8_t(d)) << 24);
		}

		/** @brief Returns `true` if a chunk tag marks editor-only data, which is dropped when a level is exported */
		static constexpr bool IsEditorOnlyChunk(std::uint32_t tag) {
			return ((tag & 0xFF) >= 'a' && (tag & 0xFF) <= 'z');
		}
	};

	/**
		@brief Level properties, the first section of the body
	*/
	struct LevelProperties
	{
		/** @brief Display name */
		String DisplayName;
		/** @brief Who made the level, empty if it isn't known */
		String Author;
		/** @brief Next level (`episode/level`, or just `level` within the same episode) */
		String NextLevel;
		/** @brief Secret level */
		String SecretLevel;
		/** @brief Bonus level */
		String BonusLevel;
		/** @brief Default (main) tile set, a path relative to `Tilesets/` without the extension */
		String DefaultTileSet;
		/** @brief Music file name with its extension */
		String Music;
		/** @brief Ambient color as `R | G << 8 | B << 16 | A << 24`, where alpha is the starting light intensity */
		std::uint32_t AmbientColor;
		/** @brief Weather type (see @ref WeatherType) */
		std::uint8_t Weather;
		/** @brief Weather intensity */
		std::uint8_t WeatherIntensity;
		/** @brief Water level in pixels */
		std::uint16_t WaterLevel;
		/** @brief Tile shown as the preview of the level */
		std::uint16_t CaptionTileId;
	};

	/**
		@brief Named palette of a level
	*/
	struct PaletteRecord
	{
		/** @brief Palette name */
		String Name;
		/** @brief Colors as `R | G << 8 | B << 16 | A << 24` */
		std::uint32_t Colors[LevelFile::ColorsPerPalette];
	};

	/**
		@brief Extra tile set used by a level

		The tiles of the extra tile sets continue the tile IDs of the default tile set, in the order the tile sets
		are listed. An @ref Embedded one is followed by a @cpp varuint @ce size and the bytes of its tile set file.
	*/
	struct ExtraTileSetRecord
	{
		/** @brief The 8-bit tiles are remapped to the level palette, by @ref Remapping or @ref AlternatePalette */
		static constexpr std::uint8_t Remapped = 0x01;
		/** @brief The tile set has true color tiles */
		static constexpr std::uint8_t TrueColor = 0x02;
		/** @brief The tile set is stored in the level itself, in @ref Data, its tiles aren't in any tile set file */
		static constexpr std::uint8_t Embedded = 0x04;

		/** @brief Combination of @ref Remapped, @ref TrueColor and @ref Embedded */
		std::uint8_t Flags;
		/** @brief Tile set path relative to `Tilesets/` without the extension, only a name of it if it's @ref Embedded */
		String Name;
		/** @brief First tile taken from the tile set */
		std::uint16_t Offset;
		/** @brief Number of tiles taken from the tile set */
		std::uint16_t Count;
		/** @brief Palette index remapping, only stored if the tile set is @ref Remapped and not @ref TrueColor */
		std::uint8_t Remapping[LevelFile::ColorsPerPalette];
		/** @brief Index of an additional palette, only stored if the tile set is both @ref Remapped and @ref TrueColor */
		std::uint8_t AlternatePalette;
		/** @brief The whole tile set as its file stores it (see @ref TileSetFile), only if it's @ref Embedded */
		SmallVector<std::uint8_t, 0> Data;
	};

	/**
		@brief Tile with a diffuse overridden by the level
	*/
	struct TileDiffuseOverride
	{
		/** @brief Tile ID */
		std::uint16_t TileId;
		/** @brief Palette indices of all pixels, row by row */
		std::uint8_t Indices[LevelFile::TileSize * LevelFile::TileSize];
	};

	/**
		@brief Tile with a collision mask overridden by the level
	*/
	struct TileMaskOverride
	{
		/** @brief Tile ID */
		std::uint16_t TileId;
		/** @brief One byte per pixel, row by row, non-zero is solid */
		std::uint8_t Mask[LevelFile::TileSize * LevelFile::TileSize];
	};

	/**
		@brief A tile of a layer, or a frame of an animated tile
	*/
	struct TileRecord
	{
		/** @brief The tile is flipped horizontally */
		static constexpr std::uint8_t FlipX = 0x01;
		/** @brief The tile is flipped vertically */
		static constexpr std::uint8_t FlipY = 0x02;
		/** @brief Mask of the modifier stored in the high nibble of @ref Flags */
		static constexpr std::uint8_t ModifierMask = 0xF0;
		/** @brief Modifier of a translucent tile */
		static constexpr std::uint8_t ModifierTranslucent = 0x10;
		/** @brief Modifier of an invisible tile */
		static constexpr std::uint8_t ModifierInvisible = 0x20;

		/** @brief Flip flags in the low nibble, a modifier in the high nibble */
		std::uint8_t Flags;
		/** @brief Tile ID, an animated tile if at least the first animated tile ID */
		std::uint16_t TileId;
	};

	/**
		@brief An animated tile
	*/
	struct AnimatedTileRecord
	{
		/** @brief Frame duration multiplied by 16 */
		std::uint16_t FrameDuration16;
		/** @brief Delay between loops */
		std::uint16_t Delay;
		/** @brief Random jitter added to @ref Delay */
		std::uint16_t DelayJitter;
		/** @brief Delay before playing backwards in the ping-pong mode */
		std::uint16_t PingPongDelay;
		/** @brief Non-zero if the animation plays forward and then backwards */
		std::uint8_t IsPingPong;
		/** @brief Frames of the animation, at most 255 */
		SmallVector<TileRecord, 0> Frames;
	};

	/**
		@brief Kind of a layer
	*/
	enum class LayerKind : std::uint8_t
	{
		Other,			/**< Any other layer */
		Sky,			/**< Sky layer of an original level (informational only) */
		Sprite			/**< The sprite (main) layer, where objects live and events are placed */
	};

	/**
		@brief Description of a layer

		Everything after @ref Height is stored only for layers other than the sprite one, which is always drawn
		with its defaults. The speed models and the renderer type are kept as raw values, so a level saved again
		keeps even values this version of the game doesn't know.
	*/
	struct LayerRecord
	{
		/** @brief The layer repeats horizontally */
		static constexpr std::uint16_t RepeatX = 0x01;
		/** @brief The layer repeats vertically */
		static constexpr std::uint16_t RepeatY = 0x02;
		/** @brief The layer uses the inherent offset of the original game */
		static constexpr std::uint16_t UseInherentOffset = 0x04;
		/** @brief The layer is visible */
		static constexpr std::uint16_t Visible = 0x08;

		/** @brief Kind of the layer */
		LayerKind Kind;
		/** @brief Combination of @ref RepeatX, @ref RepeatY, @ref UseInherentOffset and @ref Visible */
		std::uint16_t Flags;
		/** @brief Width in tiles */
		std::int32_t Width;
		/** @brief Height in tiles */
		std::int32_t Height;
		/** @brief Speed model (see @ref Tiles::LayerSpeedModel), horizontal in the low nibble, vertical in the high one */
		std::uint8_t SpeedModels;
		/** @brief Horizontal offset */
		float OffsetX;
		/** @brief Vertical offset */
		float OffsetY;
		/** @brief Horizontal speed */
		float SpeedX;
		/** @brief Vertical speed */
		float SpeedY;
		/** @brief Horizontal auto speed */
		float AutoSpeedX;
		/** @brief Vertical auto speed */
		float AutoSpeedY;
		/** @brief Depth, the layer is in front of the sprites if below 50 */
		std::int16_t Depth;
		/** @brief Renderer type (see @ref Tiles::LayerRendererType) */
		std::uint8_t RendererType;
		/** @brief Color whose meaning depends on the renderer type --- for a tinted layer, red is a palette index */
		std::uint8_t Color[4];
	};

	/**
		@brief An event placed in a level
	*/
	struct EventRecord
	{
		/** @brief The parameters are all zero and aren't stored, derived by the writer */
		static constexpr std::uint8_t NoParams = 0x01;
		/** @brief The event is spawned by a generator, @ref GeneratorFlags and @ref GeneratorDelay are stored */
		static constexpr std::uint8_t Generator = 0x02;
		/** @brief The object is illuminated */
		static constexpr std::uint8_t Illuminated = 0x04;
		/** @brief The event is present in the easy difficulty */
		static constexpr std::uint8_t Easy = 0x10;
		/** @brief The event is present in the normal difficulty */
		static constexpr std::uint8_t Normal = 0x20;
		/** @brief The event is present in the hard difficulty */
		static constexpr std::uint8_t Hard = 0x40;
		/** @brief Mask of all difficulties --- an event of none of them is present in all of them */
		static constexpr std::uint8_t DifficultyMask = 0x70;
		/** @brief The event is present only in multiplayer */
		static constexpr std::uint8_t MultiplayerOnly = 0x80;

		/** @brief Event type (see @ref EventType), raw so unknown types are kept */
		std::uint16_t Type;
		/** @brief Event flags */
		std::uint8_t Flags;
		/** @brief Generator flags, bit 0 means the first object is spawned only after the delay */
		std::uint8_t GeneratorFlags;
		/** @brief Generator delay in seconds */
		std::uint8_t GeneratorDelay;
		/** @brief Event parameters */
		std::uint8_t Params[LevelFile::EventParamsSize];

		/** @brief Returns `true` if any parameter is non-zero */
		bool HasParams() const;
	};

	/**
		@brief An event placed at any position, not aligned to the tile grid
	*/
	struct OffGridEventRecord
	{
		/** @brief Horizontal position in pixels */
		std::uint32_t X;
		/** @brief Vertical position in pixels */
		std::uint32_t Y;
		/** @brief The event */
		EventRecord Event;
	};

	/**
		@brief Optional tagged data at the end of the body
	*/
	struct ChunkRecord
	{
		/** @brief Four-character tag (see @ref LevelFile::MakeChunkTag()) */
		std::uint32_t Tag;
		/** @brief Payload */
		SmallVector<std::uint8_t, 0> Data;
	};

	/**
		@brief Reads the body of a level, section by section

		The sections have to be read in the order they are declared in, every one of them, because the body is
		a single compressed stream. The input is buffered, so reading a value doesn't go through the decompressing
		stream every time.
	*/
	class LevelReader
	{
	public:
		/** @brief Starts reading the body, the stream is left after the header by @ref LevelFile::ReadHeader() */
		LevelReader(Stream& s, const LevelHeader& header);

		LevelReader(const LevelReader&) = delete;
		LevelReader& operator=(const LevelReader&) = delete;

		/** @brief Returns `false` if the body is truncated or corrupted */
		bool IsValid() const {
			return !_failed;
		}

		/** @brief Returns the header the body belongs to */
		const LevelHeader& GetHeader() const {
			return _header;
		}

		/** @brief Reads the level properties */
		void ReadProperties(LevelProperties& properties);
		/**
		 * @brief Reads the level palette and the additional palettes
		 *
		 * The level palette is stored only with @ref LevelFlags::UseLevelPalette, so @p levelPalette (256 entries)
		 * is filled only then, which is what the return value tells. Either pointer may be `nullptr` to skip it.
		 */
		bool ReadPalettes(std::uint32_t* levelPalette, SmallVectorImpl<PaletteRecord>* additionalPalettes);
		/** @brief Reads the extra tile sets */
		void ReadExtraTileSets(SmallVectorImpl<ExtraTileSetRecord>& tileSets);
		/** @brief Reads the overridden tile diffuse, `nullptr` skips them */
		void ReadTileDiffuseOverrides(SmallVectorImpl<TileDiffuseOverride>* overrides);
		/** @brief Reads the overridden tile masks, `nullptr` skips them */
		void ReadTileMaskOverrides(SmallVectorImpl<TileMaskOverride>* overrides);
		/** @brief Reads the level texts */
		void ReadTexts(SmallVectorImpl<String>& texts);
		/** @brief Reads the first animated tile ID and the animated tiles */
		void ReadAnimatedTiles(std::uint16_t& animatedTilesOffset, SmallVectorImpl<AnimatedTileRecord>& animatedTiles);
		/** @brief Reads the number of layers */
		std::int32_t ReadLayerCount();
		/** @brief Reads the description of the next layer, its tiles have to be read next */
		void ReadLayerHeader(LayerRecord& layer);
		/**
		 * @brief Reads the tiles of the layer whose description was read last
		 *
		 * The tiles are handed over in blocks, the callback gets the index of the first tile of the block (row by
		 * row) and the tiles. Without a callback the tiles are skipped.
		 */
		void ReadLayerTiles(const LayerRecord& layer, Function<void(std::int32_t, ArrayView<const TileRecord>)>&& callback);
		/**
		 * @brief Reads the event grid
		 *
		 * It has the size of the sprite layer. The callback is called for every tile, empty ones included.
		 */
		void ReadEvents(std::int32_t width, std::int32_t height, Function<void(std::int32_t, std::int32_t, const EventRecord&)>&& callback);
		/** @brief Reads the off-grid events */
		void ReadOffGridEvents(SmallVectorImpl<OffGridEventRecord>& events);
		/**
		 * @brief Skips all sections after the level properties, so @ref ReadChunks() can be called next
		 *
		 * For a reader that needs only the properties and the chunks, as a preview of the level. Returns `false` if
		 * the body is truncated or corrupted.
		 */
		bool SkipToChunks();
		/**
		 * @brief Reads the optional chunks at the end of the body
		 *
		 * Editor-only chunks (see @ref LevelFile::IsEditorOnlyChunk()) are skipped unless @p includeEditorOnly is set.
		 */
		void ReadChunks(SmallVectorImpl<ChunkRecord>& chunks, bool includeEditorOnly);

	private:
#if defined(DEATH_TARGET_CONSTRAINED_MEMORY)
		static constexpr std::int32_t BufferSize = 1024;
#else
		static constexpr std::int32_t BufferSize = 4096;
#endif

		LevelHeader _header;
		Death::IO::Compression::DeflateStream _input;
		std::int32_t _bufferPos;
		std::int32_t _bufferLength;
		bool _failed;
		std::uint8_t _buffer[BufferSize];

		bool Refill();
		bool ReadBytes(void* destination, std::int32_t size);
		bool SkipBytes(std::int64_t size);
		std::uint8_t ReadUint8();
		std::uint16_t ReadUint16();
		std::int16_t ReadInt16();
		std::uint32_t ReadUint32();
		std::int32_t ReadInt32();
		float ReadFloat();
		std::uint32_t ReadVariableUint32();
		String ReadString8();
		String ReadString16();
		void ReadEvent(EventRecord& event);
	};

	/**
		@brief Writes a level, section by section

		The sections have to be written in the order they are declared in, every one of them, then the level is
		completed by @ref Finish(). The header is written right away.
	*/
	class LevelWriter
	{
	public:
		/** @brief Starts writing a level into the stream */
		LevelWriter(Stream& target, LevelFlags flags, std::int32_t compressionLevel = 9);

		LevelWriter(const LevelWriter&) = delete;
		LevelWriter& operator=(const LevelWriter&) = delete;

		/** @brief Writes the level properties, strings longer than 255 bytes are cut */
		void WriteProperties(const LevelProperties& properties);
		/**
		 * @brief Writes the level palette and the additional palettes
		 *
		 * The level palette (256 entries) is written only with @ref LevelFlags::UseLevelPalette, which then requires it.
		 */
		void WritePalettes(const std::uint32_t* levelPalette, ArrayView<const PaletteRecord> additionalPalettes);
		/** @brief Writes the extra tile sets */
		void WriteExtraTileSets(ArrayView<const ExtraTileSetRecord> tileSets);
		/** @brief Writes the overridden tile diffuse */
		void WriteTileDiffuseOverrides(ArrayView<const TileDiffuseOverride> overrides);
		/** @brief Writes the overridden tile masks */
		void WriteTileMaskOverrides(ArrayView<const TileMaskOverride> overrides);
		/** @brief Writes the level texts, at most 255 of them */
		void WriteTexts(ArrayView<const String> texts);
		/** @brief Writes the first animated tile ID and the animated tiles */
		void WriteAnimatedTiles(std::uint16_t animatedTilesOffset, ArrayView<const AnimatedTileRecord> animatedTiles);
		/** @brief Writes the number of layers, at most 255 */
		void WriteLayerCount(std::int32_t count);
		/** @brief Writes the description of the next layer, its tiles have to be written next */
		void WriteLayerHeader(const LayerRecord& layer);
		/** @brief Writes tiles of the layer whose description was written last, it can be called repeatedly */
		void WriteLayerTiles(ArrayView<const TileRecord> tiles);
		/**
		 * @brief Writes an event of the grid, it has to be called for every tile of the sprite layer, row by row
		 *
		 * @ref EventRecord::NoParams is derived from the parameters, whatever its value in @p event.
		 */
		void WriteEvent(const EventRecord& event);
		/** @brief Writes the off-grid events */
		void WriteOffGridEvents(ArrayView<const OffGridEventRecord> events);
		/**
		 * @brief Writes the optional chunks
		 *
		 * Nothing at all is written if there are no chunks left after editor-only ones were dropped as requested.
		 */
		void WriteChunks(ArrayView<const ChunkRecord> chunks, bool stripEditorOnly);
		/** @brief Completes the level, returns `false` on failure */
		bool Finish();

	private:
		Stream* _target;
		LevelFlags _flags;
		MemoryStream _body;
		std::unique_ptr<Death::IO::Compression::DeflateWriter> _deflate;
		std::int64_t _layerTilesLeft;
		std::int64_t _eventsLeft;
		bool _finished;

		void WriteString8(StringView value);
		void WriteEventFields(const EventRecord& event);
	};
}
