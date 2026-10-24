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
		A tag whose first letter is uppercase and second lowercase is a critical chunk (see @ref IsCriticalChunk()),
		data the level can't be played correctly without --- a reader that doesn't know one refuses the level instead
		of skipping it. Readers before 4.0.0 skip even those, so a critical chunk is only for data whose absence
		breaks nothing worse than the look of the level there.

		@par Evolving the layout
		Only the chunks and the bits of the header flags the readers ignore can be added without a new version.
		The bits of the record flags that aren't defined are reserved: they change the layout of a record, so
		a reader refuses a level with any of them set (since 4.0.0) rather than misreading the rest of it, and
		a writer never sets them. The same holds for a layer kind that isn't defined. The only exception is
		@ref EventRecord::Reserved, which 3.9.0 wrote as it was given --- a reader drops it, so it can't be given
		a meaning in this version of the layout. An offset or a speed of a layer, or a parameter of a textured layer,
		that isn't a finite number is replaced by its default, by the writer as well as by the reader.

		@par Limits
		A reader refuses a level beyond the limits below (@ref MaxLayerSize, @ref MaxLayerTiles, @ref MaxTotalLayerTiles, @ref MaxOffGridEvents,
		@ref MaxTileOverrides, @ref MaxBlobSize, @ref MaxBodySize), such a level can't come from any editor and would
		only exhaust the memory.

		@par Version history
		- 1 --- first versioned layout; a layer may use @ref Tiles::LayerRendererType::XRay and
		  @ref Tiles::LayerRendererType::BlurBehind; optional chunks at the end of the body; @ref LevelProperties::Author
		  after the display name. Since 4.0.0 a layer may also use @ref Tiles::LayerRendererType::PaletteShift, which
		  3.9.0 draws as an ordinary layer modulated by the layer's color (see @ref LayerRecord::Color), and the
		  sprite layer may be drawn otherwise than as ordinary tiles (see @ref SpriteLayerChunk), which 3.9.0 doesn't.
		  Since 4.0.0 a layer may also be a reflection (@ref Tiles::LayerRendererType::Reflection), which 3.9.0 draws as
		  an ordinary layer of its tiles modulated by its color, and a tunnel or a reflection may have parameters its
		  color has no room for (see @ref TexturedLayerChunk), which 3.9.0 ignores. Since 4.0.0 a layer may also be a
		  cylinder (@ref Tiles::LayerRendererType::Cylinder), with parameters in the same chunk, which 3.9.0 draws as an
		  ordinary layer of its tiles modulated by its color. Since 4.0.0 a layer may also be a wave
		  (@ref Tiles::LayerRendererType::Wave), with parameters in the same chunk, which 3.9.0 draws as an ordinary
		  layer of its tiles, and a tile may have the heat modifier (@ref TileRecord::ModifierHeat), which 3.9.0 draws
		  as an ordinary tile. Since 4.0.0 the layers may have names (@ref LayerNamesChunk) and the numbers they had in
		  the original game (@ref LayerNumbersChunk), which the game uses for scripts only. Since 4.0.0 a layer may also
		  be drawn in a single color (@ref Tiles::LayerRendererType::SingleColor), the sprite layer too, or only over a
		  color of what is behind it (@ref Tiles::LayerRendererType::ChromaKey), which 3.9.0 draws as an ordinary layer
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

		/** @brief Largest width and height of a layer in tiles, as large as 3.9.0 lets the editor make a layer */
		static constexpr std::int32_t MaxLayerSize = 65535;
		/** @brief Largest number of tiles of a single layer */
		static constexpr std::int64_t MaxLayerTiles = std::int64_t(1) << 26;
		/** @brief Largest number of tiles of all layers together */
		static constexpr std::int64_t MaxTotalLayerTiles = std::int64_t(1) << 27;
		/** @brief Largest number of off-grid events, each of them takes a tile of the event map of its own */
		static constexpr std::uint32_t MaxOffGridEvents = 65535;
		/** @brief Largest number of overridden tile diffuses, and of overridden tile masks */
		static constexpr std::uint32_t MaxTileOverrides = 65536;
		/** @brief Largest size of an embedded tile set or of the payload of a chunk in bytes */
		static constexpr std::uint32_t MaxBlobSize = 256 * 1024 * 1024;
		/** @brief Largest size of the decompressed body in bytes */
		static constexpr std::int64_t MaxBodySize = std::int64_t(1) << 30;

		/**
		 * @brief Small image of the level shown in the list of custom levels, kept when the level is exported
		 *
		 * @cpp u16 @ce width and height, then @cpp R, G, B @ce bytes for each pixel, row by row. Made by
		 * @ref LevelPreview, written by the level editor whenever it saves a level, and by the converter of the
		 * original levels for those of no episode, the ones the list shows.
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

		/**
		 * @brief Returns `true` if a chunk tag marks data the level can't be played without
		 *
		 * The first letter uppercase and the second lowercase, such as `Gscr`. A reader that doesn't know such
		 * a chunk refuses the level. Editor-only chunks and the preview (`PRVW`) are never critical.
		 */
		static constexpr bool IsCriticalChunk(std::uint32_t tag) {
			return ((tag & 0xFF) >= 'A' && (tag & 0xFF) <= 'Z' && ((tag >> 8) & 0xFF) >= 'a' && ((tag >> 8) & 0xFF) <= 'z');
		}

		/**
		 * @brief Returns `true` if a tile set path of a level is safe to look up in `Tilesets/`
		 *
		 * A path may have directories, but it must stay relative and must not go up from where it starts. A part of it
		 * must not end with a dot or a space, nor be a name of a device of Windows, which would resolve to something else there.
		 */
		static bool IsSafeTileSetPath(StringView path);
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
		/** @brief All the flags defined, any other one makes the level unreadable (see @ref LevelFile) */
		static constexpr std::uint8_t KnownFlags = Remapped | TrueColor | Embedded;

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
		/**
		 * @brief Modifier of a tile whose rows of pixels are drawn wavily, as if seen through heat (the heat effect tile type of JJ2+)
		 *
		 * See @ref Tiles::LayerTileFlags::Heat. Readers before 4.0.0 draw such a tile as an ordinary one.
		 */
		static constexpr std::uint8_t ModifierHeat = 0x30;
		/** @brief The flip flags defined, the record has a fixed size, so the other bits are only ignored */
		static constexpr std::uint8_t KnownFlipFlags = FlipX | FlipY;

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
		@brief Parameters of a textured layer that the color of its record has no room for

		Taken by a tunnel (@ref Tiles::LayerRendererType::Tunnel), a wave (@ref Tiles::LayerRendererType::Wave), a cylinder
		(@ref Tiles::LayerRendererType::Cylinder) and a reflection (@ref Tiles::LayerRendererType::Reflection), as the same
		fields of a layer of JJ2+ (`jjLAYER.tunnel`, `jjLAYER.wave`, `jjLAYER.cylinder` and `jjLAYER.reflection`), which
		share them there too. They aren't a part of @ref LayerRecord in the file, they're stored in @ref TexturedLayerChunk
		--- a layer without it has the defaults.
	*/
	struct TexturedLayerParams
	{
		/** @brief The circles of a tunnel spiral inwards into the distance */
		static constexpr std::uint8_t Spiral = 0x01;
		/** @brief A cylinder is drawn at half size, four times across the view instead of once and more curved (`jjLAYER.cylinder.halfSize` of JJ2+) */
		static constexpr std::uint8_t HalfSize = 0x08;
		/**
		 * @brief The vertical waves of a wave move the other way, so both move up and right, or down and left (`jjLAYER.wave.distortionAngle` of JJ2+)
		 *
		 * Without it, a positive speed moves the horizontal waves up and the vertical ones left.
		 */
		static constexpr std::uint8_t DistortionAngle = 0x10;
		/**
		 * @brief A reflection or a textured background (but a wave) is drawn only where the layer is, as a window into one of the whole view (`SURFACE::INNERWINDOW` of JJ2+)
		 *
		 * The layer is placed as an ordinary one, the horizon and the vanishing point are still a fraction of the view.
		 * Since 4.0.0 also a textured background, 3.9.0 draws one over the whole view.
		 */
		static constexpr std::uint8_t InnerWindow = 0x02;
		/**
		 * @brief A reflection or a textured background (but a wave) is drawn only where the layer is, the horizon and the vanishing point a fraction of the layer (`SURFACE::INNERLAYER` of JJ2+)
		 *
		 * A dimension in which the layer repeats is that of the view. Takes precedence over @ref InnerWindow. Since 4.0.0
		 * also a textured background, 3.9.0 draws one over the whole view.
		 */
		static constexpr std::uint8_t InnerLayer = 0x04;
		/**
		 * @brief A sky, a tunnel or a cylinder is drawn as `SURFACE::FULLSCREEN` of JJ2+
		 *
		 * In the perspective of JJ2+, its texture placed by the speeds and the offsets of the layer as an ordinary layer would
		 * be (see @ref Tiles::LayerSpeedModel::AlwaysOnTop for the sky layer of the original game). JJ2+ draws `SURFACE::LEGACY`
		 * of the sky layer so too. Without it, as the sky layers have always been drawn here, the speeds of the layer aside.
		 * The inner surfaces (@ref InnerWindow, @ref InnerLayer) and a cylinder are always drawn in the perspective of JJ2+.
		 * Since 4.0.0.
		 */
		static constexpr std::uint8_t Fullscreen = 0x20;
		/**
		 * @brief Only the ground of a sky is drawn, below its horizon, and only in the tiles of layer 8
		 *
		 * As JJ2+ draws a textured layer other than the sky layer of a level without extended layer data (`SURFACE::LEGACY`),
		 * measured: it shows only where layer 8 (see @ref LayerNumbersChunk) has tiles, layer 8 fixed to the view and
		 * repeating as it does. Readers before 4.0.0 draw the sky too.
		 */
		static constexpr std::uint8_t FloorOnly = 0x40;

		/** @brief Default of @ref Fade, a tunnel fades out into its center and a cylinder into its horizon, a reflection is tinted with the middle of the gradient of the textures of the original game */
		static constexpr std::uint8_t DefaultFade = 192;

		/**
		 * @brief Horizontal position of the vanishing point of a sky, of the center of a tunnel or of the horizon of a cylinder, of the pivot of a tile menu, or of the vanishing point of a reflection, as a fraction of the view (or of the layer, see @ref InnerLayer)
		 *
		 * For a wave, how far its horizontal waves move the texture, in units of 128 pixels (`jjLAYER.wave.amplitudeX` of JJ2+).
		 * A sky and a tile menu take it since 4.0.0.
		 */
		float FadeX = 0.5f;
		/**
		 * @brief Vertical position of the horizon of a sky, of the center of a tunnel or of the horizon of a cylinder, of the pivot of a tile menu, or of the horizon of a reflection, as a fraction of the view (or of the layer, see @ref InnerLayer)
		 *
		 * For a wave, how far its vertical waves move the texture, in units of 128 pixels (`jjLAYER.wave.amplitudeY` of JJ2+).
		 * A sky and a tile menu take it since 4.0.0.
		 */
		float FadeY = 0.5f;
		/** @brief Non-zero if a sky fades out into its horizon (since 4.0.0), a tunnel into its center or a cylinder into its horizon, the palette entry a reflection is tinted with */
		std::uint8_t Fade = DefaultFade;
		/** @brief Combination of @ref Spiral, @ref HalfSize, @ref DistortionAngle, @ref InnerWindow, @ref InnerLayer and @ref Fullscreen */
		std::uint8_t Flags = 0;
		/**
		 * @brief Horizontal speed of the texture of a reflection drawn only where the layer is, against the view (`jjLAYER.xInnerSpeed` of JJ2+)
		 *
		 * The speeds of the layer place it as an ordinary one then (see @ref InnerWindow and @ref InnerLayer), the inner
		 * speeds move its texture inside it, as the speeds of a reflection over the whole view move its texture.
		 */
		float InnerSpeedX = 0.0f;
		/** @brief Vertical speed of the texture of a reflection drawn only where the layer is, against the view (`jjLAYER.yInnerSpeed` of JJ2+) */
		float InnerSpeedY = 0.0f;
		/** @brief Horizontal speed the texture of a reflection drawn only where the layer is moves at by itself (`jjLAYER.xInnerAutoSpeed` of JJ2+, but in pixels each frame, see @ref LayerRecord::AutoSpeedX) */
		float InnerAutoSpeedX = 0.0f;
		/** @brief Vertical speed the texture of a reflection drawn only where the layer is moves at by itself (`jjLAYER.yInnerAutoSpeed` of JJ2+) */
		float InnerAutoSpeedY = 0.0f;

		/** @brief Returns `true` if all the parameters are the defaults */
		bool IsDefault() const {
			return (FadeX == 0.5f && FadeY == 0.5f && Fade == DefaultFade && Flags == 0 && HasDefaultInnerSpeeds());
		}

		/** @brief Returns `true` if all the inner speeds are zero */
		bool HasDefaultInnerSpeeds() const {
			return (InnerSpeedX == 0.0f && InnerSpeedY == 0.0f && InnerAutoSpeedX == 0.0f && InnerAutoSpeedY == 0.0f);
		}
	};

	/**
		@brief Description of a layer

		Everything after @ref Height is stored only for layers other than the sprite one, which is always placed
		with its defaults --- how it's drawn is stored apart from the record, see @ref SpriteLayerChunk. The speed
		models and the renderer type are kept as raw values, so a level saved again keeps even values this version
		of the game doesn't know.
	*/
	struct LayerRecord
	{
		/** @brief The layer repeats horizontally */
		static constexpr std::uint16_t RepeatX = 0x01;
		/** @brief The layer repeats vertically */
		static constexpr std::uint16_t RepeatY = 0x02;
		/**
		 * @brief The layer uses the inherent offset of the original game
		 *
		 * `limitVisibleRegion` of JJ2+ --- the layer is moved down by half of how much taller the view is than 200
		 * pixels. Since 4.0.0 only a layer that doesn't repeat vertically and whose vertical speed model is
		 * @ref Tiles::LayerSpeedModel::Default is moved, as in JJ2+; 3.9.0 moves any layer that has the flag.
		 */
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
		/**
		 * @brief Speed model (see @ref Tiles::LayerSpeedModel), horizontal in the low nibble, vertical in the high one
		 *
		 * @ref Tiles::LayerSpeedModel::FromStart (4) is written since 4.0.0. 3.9.0 places a layer of a value it doesn't
		 * know as one of @ref Tiles::LayerSpeedModel::Default, and so a layer of that model too, and places a layer of
		 * @ref Tiles::LayerSpeedModel::FitLevel and @ref Tiles::LayerSpeedModel::SpeedMultipliers differently, by the
		 * center of the view and the size of the level instead of the range the view moves in.
		 */
		std::uint8_t SpeedModels;
		/** @brief Horizontal offset */
		float OffsetX;
		/** @brief Vertical offset */
		float OffsetY;
		/** @brief Horizontal speed */
		float SpeedX;
		/** @brief Vertical speed */
		float SpeedY;
		/**
		 * @brief Horizontal auto speed
		 *
		 * In pixels each of 60 frames a second, the original game and JJ2+ take theirs each of 70 ticks a second (see
		 * @ref Tiles::LayerDescription::FromOriginalAutoSpeed()). With @ref Tiles::LayerSpeedModel::SpeedMultipliers,
		 * the multiplier of the size of the view at the end of the level instead.
		 */
		float AutoSpeedX;
		/** @brief Vertical auto speed, see @ref AutoSpeedX */
		float AutoSpeedY;
		/** @brief Depth, the layer is in front of the sprites if below 50 */
		std::int16_t Depth;
		/** @brief Renderer type (see @ref Tiles::LayerRendererType) */
		std::uint8_t RendererType;
		/**
		 * @brief Color whose meaning depends on the renderer type
		 *
		 * For a tinted layer, red is a palette index. For a palette-shifted layer, red is the shift and alpha the
		 * opacity; green and blue are written as 255, so a reader that doesn't know the renderer (it draws an
		 * ordinary layer modulated by the color) only lowers the red of the tiles. For a sky or a tunnel, red, green
		 * and blue are the color it fades into and non-zero alpha shows stars, for a cylinder the same without the
		 * stars. For a wave, red and green are the wavelengths of its horizontal and vertical waves (0 for none), blue
		 * its speed as a signed byte, and alpha is 0. For a reflection, red is the opacity of its tint, green the
		 * distance of its plane, blue its distortion and non-zero alpha blurs it.
		 */
		std::uint8_t Color[4];
		/**
		 * @brief Parameters of a tunnel, a wave, a cylinder or a reflection that @ref Color has no room for
		 *
		 * Not stored in the record itself but in @ref TexturedLayerChunk, the reader of the record leaves the defaults.
		 */
		TexturedLayerParams Textured;
		/**
		 * @brief Number of the layer in the original game, 1 to 8 from the front, or 0 for a layer of another origin
		 *
		 * Not stored in the record itself but in @ref LayerNumbersChunk, the reader of the record sets 0.
		 */
		std::uint8_t OriginalNumber;
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
		/** @brief Reserved, 3.9.0 could write it with any meaning, so a reader drops it and a writer never sets it (see @ref LevelFile) */
		static constexpr std::uint8_t Reserved = 0x08;
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

		/**
		 * @brief First of the last three bytes of @ref Params, which a converted level keeps the 20 bits of parameters of the original event in
		 *
		 * The game reads them of no event, only scripts of JJ2+ do, which keep data of their own in the bits of the
		 * parameters the game ignores. Not kept for an event that uses the bytes itself (see
		 * @ref Compatibility::EventConverter::KeepOriginalParams()), nor by a level converted before 4.0.0.
		 */
		static constexpr std::int32_t OriginalParamsOffset = 13;

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
		@brief Chunk with how the sprite layer is drawn, which its @ref LayerRecord doesn't store

		The record of the sprite layer ends after its size, so the layer would always be drawn as ordinary tiles. This
		ancillary chunk gives it a renderer type and a color, which mean what @ref LayerRecord::RendererType and
		@ref LayerRecord::Color of any other layer do --- but only the renderer types that keep the tiles where they
		are take effect (see @ref Tiles::LayerDescription::IsSpriteLayerRenderer()), and the layer is still placed as
		it always is. A reader that doesn't know the chunk --- the game before 4.0.0 --- draws the sprite layer as
		ordinary tiles. It's written only when the sprite layer isn't drawn so, and kept when the level is exported.

		The payload is @cpp u8 @ce renderer type and the 4 bytes of the color. A newer version may add bytes after
		them, a reader skips those it doesn't know.
	*/
	struct SpriteLayerChunk
	{
		/** @brief Tag of the chunk */
		static constexpr std::uint32_t Tag = LevelFile::MakeChunkTag('S', 'P', 'R', 'L');

		SpriteLayerChunk() = delete;

		/** @brief Reads the payload into the renderer type and the color of a record, returns `false` if it's damaged and the record stays as it was */
		static bool Read(ArrayView<const std::uint8_t> data, LayerRecord& record);
		/** @brief Writes the payload of the record of the sprite layer, nothing if the layer is drawn as ordinary tiles */
		static void Write(const LayerRecord& record, SmallVectorImpl<std::uint8_t>& data);
	};

	/**
		@brief Chunk with the parameters of tunnels, cylinders and reflections, which their @ref LayerRecord doesn't store

		A tunnel, a cylinder and a reflection take more parameters than the 4 bytes of @ref LayerRecord::Color, the rest of
		them is in @ref LayerRecord::Textured. This ancillary chunk stores them for the layers where they aren't the defaults,
		and the texture of a textured layer that has one of its own instead of its tiles.
		A reader that doesn't know the chunk --- the game before 4.0.0 --- draws every tunnel centered on the view, fading
		out into its center and not spiralling, and doesn't know cylinders and reflections at all. It's kept when the level
		is exported.

		The payload is @cpp varuint @ce count × (@cpp varuint @ce index of the layer in file order, @cpp varuint @ce size,
		then @cpp f32 @ce @ref TexturedLayerParams::FadeX, @cpp f32 @ce @ref TexturedLayerParams::FadeY, @cpp u8 @ce
		@ref TexturedLayerParams::Fade and @cpp u8 @ce @ref TexturedLayerParams::Flags). A newer version may add bytes
		at the end of an entry, a reader skips those it doesn't know.

		An entry of a layer with inner speeds or a texture of its own goes on with @cpp f32 @ce
		@ref TexturedLayerParams::InnerSpeedX, @cpp f32 @ce @ref TexturedLayerParams::InnerSpeedY, @cpp f32 @ce
		@ref TexturedLayerParams::InnerAutoSpeedX, @cpp f32 @ce @ref TexturedLayerParams::InnerAutoSpeedY and @cpp u8 @ce
		texture --- 0 for the tiles of the layer, 1 for an image of its own, @ref TextureSize × @ref TextureSize palette
		indices row by row, which follow. An entry without them, the only one before, has the defaults: no inner speeds
		and the tiles of the layer. A texture of another kind is of a newer version, the tiles of the layer are taken then.
	*/
	struct TexturedLayerChunk
	{
		/** @brief Tag of the chunk */
		static constexpr std::uint32_t Tag = LevelFile::MakeChunkTag('T', 'X', 'L', 'P');
		/** @brief Width and height of a texture of its own in pixels, as the textures of JJ2+ and of 8×8 tiles */
		static constexpr std::int32_t TextureSize = 256;

		/** @brief Parameters of one layer */
		struct Entry
		{
			/** @brief Index of the layer in file order */
			std::uint32_t Layer;
			/** @brief Parameters of the layer */
			TexturedLayerParams Params;
			/**
			 * @brief Texture of its own, @ref TextureSize × @ref TextureSize palette indices row by row
			 *
			 * The 8×8 tiles at the top left of the layer are its texture if it's empty, as `TEXTURE::FROMTILES` of JJ2+. A
			 * layer of JJ2+ with one of its own textures, or with an image of its own, has it here. It stays
			 * indexed, so it follows changes of the palette as the tiles do.
			 */
			SmallVector<std::uint8_t, 0> Texture;
		};

		TexturedLayerChunk() = delete;

		/** @brief Reads the payload, returns `false` if it's damaged and @p layers is left empty then */
		static bool Read(ArrayView<const std::uint8_t> data, SmallVectorImpl<Entry>& layers);
		/**
		 * @brief Writes the payload of the layers whose parameters aren't the defaults or that have a texture of their own, nothing if there are none
		 *
		 * An entry ends after the flags if the layer has neither inner speeds nor a texture of its own. A texture of another
		 * size than @ref TextureSize × @ref TextureSize isn't written.
		 */
		static void Write(ArrayView<const Entry> layers, SmallVectorImpl<std::uint8_t>& data);
	};

	/**
		@brief Chunk with the names of the layers

		The level editor shows them, and scripts find layers by them, the names a converted level gives its layers are
		converted into them. Written only if a layer has a name, and kept when the level is exported.

		The payload is @cpp str8 @ce per layer in file order. A layer after the last one has an empty name, the names after
		the last layer are ignored.
	*/
	struct LayerNamesChunk
	{
		/** @brief Tag of the chunk */
		static constexpr std::uint32_t Tag = LevelFile::MakeChunkTag('L', 'N', 'A', 'M');
		/** @brief Largest length of a name in bytes, a longer one is cut short at a whole character */
		static constexpr std::size_t MaxNameLength = 255;

		LayerNamesChunk() = delete;

		/** @brief Reads the name of the layer at the given index in file order from the payload, empty if it has none */
		static StringView Read(ArrayView<const std::uint8_t> data, std::int32_t layer);
		/** @brief Writes the payload of the names of all layers in file order, nothing if no layer has a name */
		static void Write(ArrayView<const StringView> names, SmallVectorImpl<std::uint8_t>& data);
	};

	/**
		@brief Chunk with the numbers the layers had in the original game

		A script of JJ2+ refers to the eight layers of a level of the original game by their numbers, 1 to 8 from the front
		(`jjLayers[n]`), whatever order they are drawn in. The converter keeps all eight of them, even an empty one, and
		gives them their numbers here; a layer of another origin --- an additional layer of a converted level, a layer added
		in the editor --- has 0. Without the chunk, scripts count the layers from the sprite layer, which is layer 4. Written only
		if a layer has a number, and kept when the level is exported.

		The payload is @cpp u8 @ce per layer in file order (see @ref LayerRecord::OriginalNumber). A layer after the last
		one has 0, the numbers after the last layer are ignored, and a number other than 1 to 8 is taken as 0.
	*/
	struct LayerNumbersChunk
	{
		/** @brief Tag of the chunk */
		static constexpr std::uint32_t Tag = LevelFile::MakeChunkTag('L', 'N', 'U', 'M');
		/** @brief Number of the layers of a level of the original game */
		static constexpr std::uint8_t OriginalLayerCount = 8;

		LayerNumbersChunk() = delete;

		/** @brief Reads the number of the layer at the given index in file order from the payload, 0 if it has none */
		static std::uint8_t Read(ArrayView<const std::uint8_t> data, std::int32_t layer);
		/** @brief Writes the payload of the numbers of all layers in file order, nothing if no layer has a number */
		static void Write(ArrayView<const std::uint8_t> numbers, SmallVectorImpl<std::uint8_t>& data);
	};

	/**
		@brief Chunk with the texts of a level as the original game had them, for its script

		The texts of a level of the original game are kept with their formatting converted, which drops the characters
		that formatting uses, such as `|`. A script of JJ2+ reads them as they were (`jjHelpStrings`), and some split them
		by those characters. The converter writes the chunk for a level with a script only, and it's kept when the level
		is exported.

		The payload is @cpp u16 @ce length and the bytes of each text in order, as the original game has them. A text
		after the last one is empty.
	*/
	struct ScriptTextsChunk
	{
		/** @brief Tag of the chunk */
		static constexpr std::uint32_t Tag = LevelFile::MakeChunkTag('S', 'T', 'X', 'T');

		ScriptTextsChunk() = delete;

		/** @brief Reads the text at the given index from the payload, empty if there's none */
		static StringView Read(ArrayView<const std::uint8_t> data, std::int32_t index);
		/** @brief Writes the payload of all texts in order, nothing if all are empty */
		static void Write(ArrayView<const StringView> texts, SmallVectorImpl<std::uint8_t>& data);
	};

	/**
		@brief Chunk with the custom weapons a level sets up, for its script

		A level of JJ2+ can keep how it changes the weapons, which its script sets up together with weapons of its own
		(see @ref Scripting::LevelScriptLoader). The converter writes the chunk for a level with such weapons, and it's
		kept when the level is exported.

		The payload is @cpp i16 @ce the argument of the hook of the weapons of the script (-1 for none), then for each
		of the 9 weapons: @cpp u8 @ce 1 if it's a weapon of the script, @cpp i32 @ce the most ammo, @cpp u8 @ce whether
		it comes from birds (2 if powered up), from gun crates, the gems lost, the gems lost powered up, 1 if it's
		infinite and 2 if it replenishes, the event of its ammo crate (0 for none), the style of the 8th weapon (255 for
		none), and @cpp u32 @ce the length and the bytes of the parameters of a weapon of the script. That's how
		`jjSTREAM` of JJ2+ reads them.
	*/
	struct CustomWeaponsChunk
	{
		/** @brief Tag of the chunk */
		static constexpr std::uint32_t Tag = LevelFile::MakeChunkTag('C', 'W', 'P', 'N');
		/** @brief Number of weapons */
		static constexpr std::int32_t WeaponCount = 9;

		CustomWeaponsChunk() = delete;
	};

	/**
		@brief Chunk with the sprites a level recolors, for its script

		A level of JJ2+ can recolor some sprites of the game, platforms, poles and the like, which its script does when it
		sets up (see @ref Scripting::LevelScriptLoader). The converter writes the chunk for a level with such sprites, and
		it's kept when the level is exported.

		The payload is, for each recolored sprite, @cpp u8 @ce its slot in the list of the sprites the editor of the
		level can recolor and @cpp u8 @ce the new palette index of each of the 256 indices.
	*/
	struct SpriteRecolorsChunk
	{
		/** @brief Tag of the chunk */
		static constexpr std::uint32_t Tag = LevelFile::MakeChunkTag('S', 'R', 'C', 'L');
		/** @brief Size of one recolored sprite in the payload */
		static constexpr std::size_t RecordSize = 1 + 256;

		SpriteRecolorsChunk() = delete;
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

		/** @brief Returns why the body couldn't be read, empty if it could */
		StringView GetError() const;

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
		 * For a reader that needs only the properties and the chunks, as a preview of the level. The data of
		 * embedded tile sets is skipped without being kept. Returns `false` if the body is truncated or corrupted.
		 */
		bool SkipToChunks();
		/**
		 * @brief Reads the optional chunks at the end of the body
		 *
		 * Editor-only chunks (see @ref LevelFile::IsEditorOnlyChunk()) are skipped unless @p includeEditorOnly is set.
		 * A critical chunk (see @ref LevelFile::IsCriticalChunk()) not listed in @p knownCriticalChunks makes the body
		 * invalid, the level must not be used then.
		 */
		void ReadChunks(SmallVectorImpl<ChunkRecord>& chunks, bool includeEditorOnly, ArrayView<const std::uint32_t> knownCriticalChunks = {});
		/**
		 * @brief Reads only the chunks @p select returns `true` for, skipping the others without keeping their payload
		 *
		 * @p select gets the tag and the size of the payload of each chunk, editor-only ones too. Critical chunks are checked
		 * as by @ref ReadChunks().
		 */
		void ReadSelectedChunks(SmallVectorImpl<ChunkRecord>& chunks, Function<bool(std::uint32_t, std::uint32_t)>&& select,
			ArrayView<const std::uint32_t> knownCriticalChunks = {});
		/**
		 * @brief Skips the optional chunks at the end of the body, only checking them
		 *
		 * For a reader that needs none of them. Returns `false` if the body is truncated or corrupted, or if it has
		 * a critical chunk not listed in @p knownCriticalChunks.
		 */
		bool SkipChunks(ArrayView<const std::uint32_t> knownCriticalChunks = {});

	private:
#if defined(DEATH_TARGET_CONSTRAINED_MEMORY)
		static constexpr std::int32_t BufferSize = 1024;
#else
		static constexpr std::int32_t BufferSize = 4096;
#endif

		LevelHeader _header;
		Death::IO::Compression::DeflateStream _input;
		std::int64_t _totalRead;
		std::int64_t _totalLayerTiles;
		const char* _error;
		std::int32_t _bufferPos;
		std::int32_t _bufferLength;
		bool _failed;
		std::uint8_t _buffer[BufferSize];

		// Marks the body as invalid, the first reason is kept
		void Fail(const char* reason);
		bool Refill();
		bool ReadBytes(void* destination, std::int32_t size);
		bool SkipBytes(std::int64_t size);
		// Reads a blob of a size read from the body, growing the buffer only as the data really comes
		bool ReadBlob(SmallVectorImpl<std::uint8_t>& data, std::uint32_t size);
		// Reads an extra tile set, the data of an embedded one is only skipped without `keepData`
		void ReadExtraTileSet(ExtraTileSetRecord& tileSet, bool keepData);
		// Reads the chunks, keeping them in `chunks` if it isn't `nullptr` and `select` takes them if there's any
		void ReadChunksInternal(SmallVectorImpl<ChunkRecord>* chunks, bool includeEditorOnly, ArrayView<const std::uint32_t> knownCriticalChunks,
			Function<bool(std::uint32_t, std::uint32_t)>* select = nullptr);
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
		completed by @ref Finish(), which writes the whole level to the target and checks every write. Nothing
		reaches the target before. @ref LevelFlags::HasPreview is derived from the chunks written, whatever its value
		in the flags given. A string, a list or a count that doesn't fit its field is cut with a warning
		logged, a string never in the middle of a UTF-8 sequence. The reserved bits of the records are never written.
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
		/** @brief Writes the optional chunks of two lists as a single one, those of @p first first, so neither has to be copied into the other */
		void WriteChunks(ArrayView<const ChunkRecord> first, ArrayView<const ChunkRecord> second, bool stripEditorOnly);
		/**
		 * @brief Completes the level and writes it to the target
		 *
		 * Returns `false` if the level couldn't be compressed, or if any write to the target or flushing it failed,
		 * such as on a full disk.
		 */
		bool Finish();

	private:
		Stream* _target;
		LevelFlags _flags;
		MemoryStream _body;
		std::unique_ptr<Death::IO::Compression::DeflateWriter> _deflate;
		std::int64_t _layerTilesLeft;
		std::int64_t _totalLayerTiles;
		std::int64_t _eventsLeft;
		bool _finished;
		// Something was given that no reader would accept, the level can't be completed
		bool _failed;
		// A preview chunk was written, see LevelFlags::HasPreview
		bool _hasPreview;

		void WriteString8(StringView value);
		void WriteEventFields(const EventRecord& event);
		// Returns how many items of a list fit a count of at most `max`, warns if not all of them
		static std::size_t FitCount(std::size_t count, std::size_t max, const char* what);
		// Returns how many bytes of a UTF-8 string fit `max` bytes, without cutting a sequence in two
		static std::size_t FitUtf8(StringView value, std::size_t max);
	};
}
