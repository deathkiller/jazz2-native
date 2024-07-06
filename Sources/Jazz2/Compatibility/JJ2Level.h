#pragma once

#include "../../Common.h"
#include "JJ2Block.h"
#include "JJ2Event.h"
#include "JJ2Version.h"
#include "EventConverter.h"
#include "../WeatherType.h"

#include <functional>

#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>
#include <IO/MemoryStream.h>

using namespace Death::Containers;
using namespace Death::IO;

namespace Jazz2::Compatibility
{
	class JJ2Episode;

	class JJ2Level // .j2l
	{
		friend class JJ2Episode;

	public:
		struct LevelToken {
			String Episode;
			String Level;
		};

		struct ExtraTilesetEntry {
			String Name;
			std::uint16_t Offset;
			std::uint16_t Count;
			bool HasPaletteRemapping;
			std::uint8_t PaletteRemapping[256];
		};

		static constexpr std::int32_t JJ2LayerCount = 8;
		static constexpr std::int32_t TextEventStringsCount = 16;

		String LevelName;
		String DisplayName;
		String Tileset;
		SmallVector<ExtraTilesetEntry, 0> ExtraTilesets;
		String Music;
		String NextLevel;
		String BonusLevel;
		String SecretLevel;

		std::uint8_t LightingMin, LightingStart;

		JJ2Level() : _version(JJ2Version::Unknown), _animCount(0), _verticalMPSplitscreen(false), _isMpLevel(false), _hasPit(false), _hasPitInstantDeath(false), _hasCTF(false), _hasLaps(false), _useLevelPalette(false) { }

		bool Open(const StringView path, bool strictParser);

		void Convert(const StringView targetPath, const EventConverter& eventConverter, const std::function<LevelToken(const StringView)>& levelTokenConversion = nullptr);
		void AddLevelTokenTextID(uint8_t textId);

		JJ2Version GetVersion() const {
			return _version;
		}
		std::int32_t GetMaxSupportedTiles() const {
			return (_version == JJ2Version::BaseGame ? 1024 : 4096);
		}
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
		std::unique_ptr<TileEventSection[]> _events;
		SmallVector<std::uint8_t, TextEventStringsCount> _levelTokenTextIds;

		void LoadMetadata(JJ2Block& block, bool strictParser);
		void LoadStaticTileData(JJ2Block& block, bool strictParser);
		void LoadAnimatedTiles(JJ2Block& block, bool strictParser);
		void LoadLayerMetadata(JJ2Block& block, bool strictParser);
		void LoadEvents(JJ2Block& block, bool strictParser);
		void LoadLayers(JJ2Block& dictBlock, std::int32_t dictLength, JJ2Block& layoutBlock, bool strictParser);
		void LoadMlleData(JJ2Block& block, uint32_t version, const StringView& path, bool strictParser);

		static void WriteLevelName(Stream& so, MutableStringView value, const std::function<LevelToken(MutableStringView&)>& levelTokenConversion = nullptr);
		static bool StringHasSuffixIgnoreCase(const StringView value, const StringView suffix);
	};
}