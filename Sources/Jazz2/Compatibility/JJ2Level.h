#pragma once

#include "../../Common.h"
#include "JJ2Block.h"
#include "JJ2Event.h"
#include "JJ2Version.h"
#include "EventConverter.h"
#include "../WeatherType.h"

#include "../../nCine/IO/GrowableMemoryFile.h"

#include <functional>

#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

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
			uint16_t Offset;
			uint16_t Count;
		};

		static constexpr int JJ2LayerCount = 8;
		static constexpr int TextEventStringsCount = 16;

		String LevelName;
		String DisplayName;
		String Tileset;
		String Music;
		String NextLevel;
		String BonusLevel;
		String SecretLevel;

		uint8_t LightingMin, LightingStart;

		JJ2Level() : _version(JJ2Version::Unknown), _verticalMPSplitscreen(false), _isMpLevel(false), _hasPit(false), _hasCTF(false), _hasLaps(false), _animCount(0) { }

		void Open(const StringView& path, bool strictParser);

		void Convert(const String& targetPath, const EventConverter& eventConverter, const std::function<LevelToken(const StringView&)>& levelTokenConversion = nullptr);
		void AddLevelTokenTextID(uint8_t textId);

		JJ2Version GetVersion() const {
			return _version;
		}
		int GetMaxSupportedTiles() const {
			return (_version == JJ2Version::BaseGame ? 1024 : 4096);
		}
		int GetMaxSupportedAnims() const {
			return (_version == JJ2Version::BaseGame ? 128 : 256);
		}

	private:
		struct LayerSection
		{
			uint32_t Flags;				// TODO: All except Parallax Stars supported
			uint8_t Type;				// Ignored
			bool Used;
			int32_t Width;
			int32_t InternalWidth;
			int32_t Height;
			int32_t Depth;
			uint8_t DetailLevel;		// Ignored
			float WaveX;				// TODO: Not supported
			float WaveY;				// TODO: Not supported
			float SpeedX;
			float SpeedY;
			float AutoSpeedX;
			float AutoSpeedY;
			uint8_t TexturedBackgroundType;
			uint8_t TexturedParams1;
			uint8_t TexturedParams2;
			uint8_t TexturedParams3;
			std::unique_ptr<uint16_t[]> Tiles;
		};

		struct TileEventSection
		{
			JJ2Event EventType;
			uint8_t Difficulty;
			bool Illuminate;
			uint32_t TileParams;		// Partially supported

			int GeneratorDelay;
			uint8_t GeneratorFlags;
			ConversionResult Converted;
		};

		struct TilePropertiesSection
		{
			TileEventSection Event;
			bool Flipped;
			uint8_t Type;				// Partially supported (Translucent: supported, Caption: ignored)
		};

		struct AnimatedTileSection
		{
			uint16_t Delay;
			uint16_t DelayJitter;
			uint16_t ReverseDelay;
			bool IsReverse;
			uint8_t Speed;
			uint8_t FrameCount;
			uint16_t Frames[64];
		};

		JJ2Version _version;

		uint16_t _animCount;
		bool _verticalMPSplitscreen, _isMpLevel;
		bool _hasPit, _hasCTF, _hasLaps;
		uint32_t _darknessColor;
		WeatherType _weatherType;
		uint8_t _weatherIntensity;

		String _textEventStrings[TextEventStringsCount];

		std::unique_ptr<LayerSection[]> _layers;
		std::unique_ptr<TilePropertiesSection[]> _staticTiles;
		std::unique_ptr<AnimatedTileSection[]> _animatedTiles;
		std::unique_ptr<TileEventSection[]> _events;
		SmallVector<uint8_t, TextEventStringsCount> _levelTokenTextIds;

		void LoadMetadata(JJ2Block& block, bool strictParser);
		void LoadStaticTileData(JJ2Block& block, bool strictParser);
		void LoadAnimatedTiles(JJ2Block& block, bool strictParser);
		void LoadLayerMetadata(JJ2Block& block, bool strictParser);
		void LoadEvents(JJ2Block& block, bool strictParser);
		void LoadLayers(JJ2Block& dictBlock, int dictLength, JJ2Block& layoutBlock, bool strictParser);
		void LoadMlleData(JJ2Block& block, uint32_t version, bool strictParser);

		static void WriteLevelName(GrowableMemoryFile& so, MutableStringView value, const std::function<LevelToken(MutableStringView&)>& levelTokenConversion = nullptr);
		static bool StringHasSuffixIgnoreCase(const StringView& value, const StringView& suffix);
	};
}