#pragma once

#include "../../Main.h"
#include "JJ2Block.h"
#include "JJ2Event.h"
#include "JJ2Version.h"
#include "EventConverter.h"
#include "../WeatherType.h"

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

	/** @brief Parses original `.j2l` level files */
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

		/** @brief Converts the level and writes the result to the specified target path */
		void Convert(StringView targetPath, EventConverter& eventConverter, Function<LevelToken(StringView)>&& levelTokenConversion = {});
		/** @brief Marks the level text with the specified ID as a level token */
		void AddLevelTokenTextID(std::uint8_t textId);

		/** @brief Returns target version of the level */
		JJ2Version GetVersion() const {
			return _version;
		}
		/** @brief Returns maximum number of supported tiles */
		std::int32_t GetMaxSupportedTiles() const {
			return (_version == JJ2Version::BaseGame ? 1024 : 4096);
		}
		/** @brief Returns maximum number of supported animations */
		std::int32_t GetMaxSupportedAnims() const {
			return (_version == JJ2Version::BaseGame ? 128 : 256);
		}

	private:
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
			std::unique_ptr<std::uint16_t[]> Tiles;
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
			std::uint16_t Frames[64];
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
		std::unique_ptr<TileEventSection[]> _events;
		SmallVector<std::uint8_t, TextEventStringsCount> _levelTokenTextIds;
		SmallVector<OffGridEvent, 0> _offGridEvents;

		void LoadMetadata(JJ2Block& block, bool strictParser);
		void LoadStaticTileData(JJ2Block& block, bool strictParser);
		void LoadAnimatedTiles(JJ2Block& block, bool strictParser);
		void LoadLayerMetadata(JJ2Block& block, bool strictParser);
		void LoadEvents(JJ2Block& block, bool strictParser);
		void LoadLayers(JJ2Block& dictBlock, std::int32_t dictLength, JJ2Block& layoutBlock, bool strictParser);
		void LoadMlleData(JJ2Block& block, std::uint32_t version, StringView path, bool strictParser);
		void CheckWaterLevelAroundStart();

		static void WriteLevelName(Stream& so, MutableStringView value, Function<LevelToken(StringView)>&& levelTokenConversion = {});
		static bool StringHasSuffixIgnoreCase(StringView value, StringView suffix);
	};
}