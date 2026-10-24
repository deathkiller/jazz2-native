#pragma once

#include "../../Main.h"
#include "JJ2Block.h"
#include "JJ2Event.h"
#include "JJ2Version.h"
#include "EventConverter.h"
#include "../WeatherType.h"
#include "../Formats/LevelFile.h"
#include "../Formats/TileSetFile.h"
#include "../Tiles/LayerTypes.h"

#include <memory>

#include <Containers/Function.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>
#include <IO/MemoryStream.h>

using namespace Death::Containers;
using namespace Death::IO;

namespace Jazz2::Compatibility
{
	class JJ2Episode;

	/**
		@brief Parses original `.j2l` level files
		
		Reads an original level layout, including its tile layers, per-tile events, animated tiles,
		palettes and MLLE extension data, and writes the converted level to the target path. Events are
		translated to the engine's native types via an @ref EventConverter during conversion.
	*/
	class JJ2Level
	{
		friend class JJ2Episode;

	public:
		/** @brief Episode name and level name */
		struct LevelToken {
			/** @brief Episode name */
			String Episode;
			/** @brief Level name */
			String Level;
		};

		/** @brief Alternate palette */
		struct AlternatePalette {
			/** @brief Palette name */
			String Name;
			/** @brief Colors (24-bit) */
			std::uint8_t Colors[256 * 3];
		};

		/** @brief Color mode of tile set */
		enum class TilesetColorMode : std::uint8_t {
			Original8bit,					/**< Original 8-bit palette is used */
			Remapped8bit,					/**< 8-bit palette is remapped according to @ref PaletteRemapping */
			Original24bit,					/**< Original 24-bit palette is used */
			AlternatePalette24bit			/**< Alternate 24-bit palette is used */
		};

		/** @brief Extra tileset used in the level */
		struct ExtraTilesetEntry {
			/** @brief Tile set name */
			String Name;
			/** @brief Offset tile index */
			std::uint16_t Offset;
			/** @brief Number of tiles */
			std::uint16_t Count;
			/** @brief Color mode */
			TilesetColorMode ColorMode;

			union {
				/** @brief Palette remapping */
				std::uint8_t PaletteRemapping[256];
				/** @brief Alternate palette mapping ID for 24-bit */
				std::uint8_t AlternatePaletteMappingID24Bit;
			};
		};

		/** @brief Number of layers in the original game */
		static constexpr std::int32_t JJ2LayerCount = 8;
		/** @brief Most layers accepted from an MLLE stream, far above what real levels use (each further 8 layers is a file of its own) */
		static constexpr std::int32_t MaxMlleLayerCount = 1024;
		/** @brief Number of level text entries in the original game */
		static constexpr std::int32_t TextEventStringsCount = 16;

		/** @brief Internal name of the level */
		String LevelName;
		/** @brief Display name of the level */
		String DisplayName;
		/** @brief Name of the tile set used by the level */
		String Tileset;
		/** @brief List of alternate palettes */
		SmallVector<AlternatePalette, 0> AlternatePalettes;
		/** @brief List of extra tile sets used by the level */
		SmallVector<ExtraTilesetEntry, 0> ExtraTilesets;
		/** @brief Name of the music track */
		String Music;
		/** @brief Name of the next level */
		String NextLevel;
		/** @brief Name of the bonus level */
		String BonusLevel;
		/** @brief Name of the secret level */
		String SecretLevel;

		/** @brief Minimum and starting lighting level */
		std::uint8_t LightingMin, LightingStart;

		/** @brief Creates a new instance */
		JJ2Level() : _version(JJ2Version::Unknown), _animCount(0), _verticalMPSplitscreen(false), _isMpLevel(false), _hasPit(false), _hasPitInstantDeath(false), _hasCTF(false), _hasLaps(false), _useLevelPalette(false) { }

		/** @brief Opens and parses the specified level file */
		bool Open(StringView path, bool strictParser);

		/**
		 * @brief Converts the level and writes the result to the specified target path
		 *
		 * With @p tilesetLookup, which converts tiles of an original tile set of a name into memory, or returns `nullptr`
		 * if there's no such tile set, the level gets a preview drawn with them (see @ref Formats::LevelPreview). It's
		 * given the indices of the tiles the preview takes, sorted, returns them in the same order (one past the end of
		 * the tile set empty) and sets the last argument to the number of tiles of the whole tile set. A level whose
		 * default tile set isn't there gets none, the tiles of an extra tile set that isn't there are left out.
		 *
		 * With @p textureLookup, which fills @ref Formats::TexturedLayerChunk::TextureSize ×
		 * @ref Formats::TexturedLayerChunk::TextureSize palette indices with a texture of JJ2+ by its number
		 * (`TEXTURE::Texture`, from 1) and returns `false` if there's no such texture, a textured layer of a level made
		 * for JJ2+ gets the texture it names. Without it, or without the texture, the layer gets a texture made up of
		 * smooth noise in the same colors, which ripples a reflection alike.
		 */
		void Convert(StringView targetPath, EventConverter& eventConverter, Function<LevelToken(StringView)>&& levelTokenConversion = {},
			Function<std::shared_ptr<const Formats::TileSetData>(StringView, ArrayView<const std::int32_t>, std::int32_t&)>&& tilesetLookup = {},
			Function<bool(std::int32_t, std::uint8_t*)>&& textureLookup = {});
		/** @brief Marks the level text with the specified ID as a level token */
		void AddLevelTokenTextID(std::uint8_t textId);
		/** @brief Records a music track that an event of the level switches to, in the form the converted level asks for it */
		void AddEventMusic(StringView music);
		/**
			@brief Returns the music tracks the events of the level switch to, filled in by @ref Convert()

			The level's own track (@ref Music) is not among them. A boss fight plays one of its own, and only the
			activating event names it, so a content tree that carries only what the levels name has to ask here.
		*/
		ArrayView<const String> GetEventMusic() const {
			return _eventMusic;
		}

		/** @brief Returns target version of the level */
		JJ2Version GetVersion() const {
			return _version;
		}
		/** @brief Returns maximum number of supported tiles */
		std::int32_t GetMaxSupportedTiles() const {
			return (_version == JJ2Version::BaseGame ? 1024 : 4096);
		}
		/**
			@brief Frames one animated tile can have

			What the format stores per tile, and the size of @ref AnimatedTileSection::Frames. The count that
			accompanies them is a single byte, so a file can name up to 255 - see LoadAnimatedTiles().
		*/
		static constexpr std::int32_t MaxAnimationFrames = 64;

		/** @brief Returns maximum number of supported animations */
		std::int32_t GetMaxSupportedAnims() const {
			return (_version == JJ2Version::BaseGame ? 128 : 256);
		}
		/** @brief Returns the first tile ID of the animated tiles, the IDs below it are the tiles of the tile sets */
		std::int32_t GetAnimatedTilesOffset() const {
			return GetMaxSupportedTiles() - _animCount;
		}

		/**
		 * @brief Converts a layer as the converted level has it, returns `false` if there's no such layer or it has no size
		 *
		 * @p index is 0 to 7 for the layers of the original, more for the additional layers of MLLE. The tile IDs are those
		 * of this level, the animated tiles from @ref GetAnimatedTilesOffset() up. A texture of its own of a textured
		 * layer isn't converted. Meant for scripts, which take layers of other levels (`jjLayersFromLevel()` of JJ2+).
		 */
		bool ConvertLayer(std::int32_t index, Formats::LayerRecord& record, SmallVectorImpl<Formats::TileRecord>& tiles) const;

	private:
		// Types of the textured background of a layer as JJ2+ takes them (TEXTURE::Style), plain JJ2 knows only the first
		static constexpr std::uint8_t TexturedStyleWarpHorizon = 0;
		static constexpr std::uint8_t TexturedStyleTunnel = 1;
		static constexpr std::uint8_t TexturedStyleMenu = 2;
		static constexpr std::uint8_t TexturedStyleTileMenu = 3;
		static constexpr std::uint8_t TexturedStyleWave = 4;
		static constexpr std::uint8_t TexturedStyleCylinder = 5;
		static constexpr std::uint8_t TexturedStyleReflection = 6;
		// The tile type of JJ2+ whose rows of pixels are drawn wavily, as if seen through heat (see Tiles::LayerTileFlags::Heat)
		static constexpr std::uint8_t TileTypeHeatEffect = 5;

		enum class LayerSectionSpeedModel {
			Normal,
			Legacy,
			BothSpeeds,
			FromStart,
			FitLevel,
			SpeedMultipliers
		};

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct LayerSection {
			std::uint32_t Flags;
			std::uint8_t Type;				// Ignored
			bool Used;
			bool Visible;
			std::uint8_t DetailLevel;		// Ignored
			std::int32_t Width;
			std::int32_t InternalWidth;
			std::int32_t Height;
			std::int32_t Depth;
			float OffsetX;
			float OffsetY;
			float SpeedX;
			float SpeedY;
			float AutoSpeedX;
			float AutoSpeedY;
			LayerSectionSpeedModel SpeedModelX;
			LayerSectionSpeedModel SpeedModelY;
			std::uint8_t TexturedBackgroundType;
			std::uint8_t TexturedParams1;
			std::uint8_t TexturedParams2;
			std::uint8_t TexturedParams3;
			std::uint8_t SpriteMode;
			std::uint8_t SpriteParam;
			// Of a textured layer, only the MLLE stream has them: where it's drawn (1 the original way, 2 the whole view, 3 and 4
			// only where the layer is, 0 if the stream doesn't say), whether it fades or the palette entry of the tint of a
			// reflection, where its fade is (the horizon of a reflection), the speeds of its texture inside the layer drawn only
			// where it is, and its texture (0 the tiles of the layer, more one of the textures of JJ2+, less an image of its
			// own, which is then in TextureImage)
			std::uint8_t TextureSurface = 0;
			std::uint8_t TextureFade = 192;
			float TextureFadeX = 0.5f;
			float TextureFadeY = 0.5f;
			float InnerSpeedX = 0.0f;
			float InnerSpeedY = 0.0f;
			float InnerAutoSpeedX = 0.0f;
			float InnerAutoSpeedY = 0.0f;
			std::int8_t TextureSource = 0;
			std::unique_ptr<std::uint8_t[]> TextureImage;
			std::unique_ptr<std::uint16_t[]> Tiles;
			// The name MLLE gave the layer, which scripts find it by (MLLE::GetLayer())
			String Name;
		};

		struct TileEventSection {
			JJ2Event EventType;
			std::uint8_t GeneratorFlags;
			std::uint8_t Difficulty;
			bool Illuminate;
			std::uint32_t TileParams;		// Partially supported

			std::int32_t GeneratorDelay;
			ConversionResult Converted;
		};

		// An event of the grid that isn't empty, by its tile index (x + y * width of the sprite layer)
		struct GridEventSection {
			std::int32_t Index;
			TileEventSection Event;
		};

		struct TilePropertiesSection {
			TileEventSection Event;
			bool Flipped;
			std::uint8_t Type;
		};

		struct AnimatedTileSection {
			std::uint16_t Delay;
			std::uint16_t DelayJitter;
			std::uint16_t ReverseDelay;
			bool IsPingPong;
			std::uint8_t Speed;
			std::uint8_t FrameCount;
			std::uint16_t Frames[MaxAnimationFrames];
		};

		struct OverridenTileDiffuse {
			std::uint16_t TileID;
			std::uint8_t Diffuse[32 * 32];
		};

		struct OverridenTileMask {
			std::uint16_t TileID;
			std::uint8_t Mask[32 * 32];
		};

		struct OffGridEvent {
			std::uint16_t X;
			std::uint16_t Y;
			
			JJ2Event EventType;
			std::uint8_t GeneratorFlags;
			std::uint8_t Difficulty;
			bool Illuminate;
			std::uint32_t TileParams;		// Partially supported

			std::int32_t GeneratorDelay;
			ConversionResult Converted;
		};
#endif

		JJ2Version _version;

		std::uint16_t _animCount;
		bool _isHidden, _verticalMPSplitscreen, _isMpLevel;
		bool _hasPit, _hasPitInstantDeath, _hasCTF, _hasLaps;
		bool _isPlusLevel;
		std::uint32_t _darknessColor;
		WeatherType _weatherType;
		std::uint8_t _weatherIntensity;
		std::uint16_t _waterLevel;

		String _textEventStrings[TextEventStringsCount];
		std::uint8_t _levelPalette[256 * 3];
		bool _useLevelPalette;

		SmallVector<LayerSection, JJ2LayerCount> _layers;
		std::unique_ptr<TilePropertiesSection[]> _staticTiles;
		std::unique_ptr<AnimatedTileSection[]> _animatedTiles;
		SmallVector<OverridenTileDiffuse, 0> _overridenTileDiffuses;
		SmallVector<OverridenTileMask, 0> _overridenTileMasks;
		// Only the tiles with an event, sorted by their index - most have none, and a whole grid of them takes tens of MB
		// for a big level, more than consoles have
		SmallVector<GridEventSection, 0> _events;
		SmallVector<std::uint8_t, TextEventStringsCount> _levelTokenTextIds;
		SmallVector<String, 0> _eventMusic;
		SmallVector<OffGridEvent, 0> _offGridEvents;

		void LoadMetadata(JJ2Block& block, bool strictParser);
		void LoadStaticTileData(JJ2Block& block, bool strictParser);
		void LoadAnimatedTiles(JJ2Block& block, bool strictParser);
		void LoadLayerMetadata(JJ2Block& block, bool strictParser);
		bool LoadEvents(JJ2Block& block, bool strictParser);
		bool LoadLayers(JJ2Block& dictBlock, JJ2Block& layoutBlock, bool strictParser);
		void LoadMlleData(JJ2Block& block, std::uint32_t version, StringView path, bool strictParser);
		void CheckWaterLevelAroundStart();
		// Returns the event of a tile of the grid, `nullptr` if it has none
		const TileEventSection* FindEvent(std::int32_t x, std::int32_t y) const;

		// Makes the record of a layer as Convert() writes it, and its texture of its own, if it has one - for a level of JJ2+
		// with the textured layers drawn as there
		void MakeLayerRecord(std::int32_t index, Formats::LayerRecord& record, SmallVectorImpl<std::uint8_t>& texture,
			Function<bool(std::int32_t, std::uint8_t*)>& textureLookup, bool forPlusLevel) const;
		// Converts a tile of a layer, with the flips and the modifier of its tile type
		Formats::TileRecord ConvertLayerTile(std::uint16_t tileIdx) const;

		static String ConvertLevelName(StringView value, Function<LevelToken(StringView)>& levelTokenConversion);
		static void ExpandPalette(const std::uint8_t* colors, std::uint32_t* palette);
		static Tiles::LayerSpeedModel ConvertSpeedModel(LayerSectionSpeedModel speedModel);
		// Sets the renderer type and the color of a layer drawn in a SPRITE::Mode of JJ2+, as MLLE sets it
		static void ConvertSpriteMode(std::uint8_t spriteMode, std::uint8_t spriteParam, Formats::LayerRecord& record);
		// Fills a texture of a textured layer with smooth noise that repeats, in the colors the textures of JJ2+ take (176 to
		// 207), for a layer whose texture of JJ2+ isn't available
		static void MakeRippleTexture(std::uint8_t* pixels);
		static Formats::EventRecord MakeEventRecord(const ConversionResult& converted, bool illuminate, std::uint8_t difficulty, std::uint8_t generatorFlags, std::int32_t generatorDelay);
		static bool StringHasSuffixIgnoreCase(StringView value, StringView suffix);
	};
}