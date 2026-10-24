#include "JJ2Level.h"
#include "JJ2Strings.h"
#include "EventConverter.h"
#include "../Formats/LevelFile.h"
#include "../Formats/LevelPreview.h"
#include "../Tiles/LayerTypes.h"

#include "../../nCine/Base/Algorithms.h"

#include <Containers/GrowableArray.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>
#include <IO/Compression/DeflateStream.h>

#include <algorithm>
#include <cmath>

using namespace Death::Containers::Literals;
using namespace Death::IO;
using namespace Death::IO::Compression;

namespace Jazz2::Compatibility
{
	namespace
	{
		// The floats of the MLLE stream are taken as they are written, so one that isn't finite (a damaged or crafted
		// level) is read as 0 rather than passed on to the converted level, whose writer refuses it
		float ReadFiniteFloat(JJ2Block& block)
		{
			float value = block.ReadFloat();
			return (std::isfinite(value) ? value : 0.0f);
		}
	}

	bool JJ2Level::Open(StringView path, bool strictParser)
	{
		auto s = fs::Open(path, FileAccess::Read);	
		if (!s->IsValid()) {
			LOGE("Cannot open file \"{}\" for reading", path);
			return false;
		}

		// Skip copyright notice
		s->Seek(180, SeekOrigin::Current);

		LevelName = fs::GetFileNameWithoutExtension(path);
		StringUtils::lowercaseInPlace(LevelName);

		JJ2Block headerBlock(s, 262 - 180);

		std::uint32_t magic = headerBlock.ReadUInt32();
		DEATH_ASSERT(magic == 0x4C56454C /*LEVL*/, "Invalid magic string", false);

		// passwordHash is 3 bytes
		headerBlock.DiscardBytes(3);

		_isHidden = headerBlock.ReadBool();

		DisplayName = headerBlock.ReadString(32, true);

		std::uint16_t version = headerBlock.ReadUInt16();
		_version = (version <= 0x202 ? JJ2Version::BaseGame : JJ2Version::TSF);

		_isPlusLevel = false;
		_darknessColor = 0;
		_weatherType = WeatherType::None;
		_weatherIntensity = 0;
		_waterLevel = 32767;

		std::int32_t recordedSize = headerBlock.ReadInt32();
		DEATH_ASSERT(!strictParser || s->GetSize() == recordedSize, "Unexpected file size", false);

		// Get the CRC; would check here if it matches if we knew what variant it is AND what it applies to
		// Test file across all CRC32 variants + Adler had no matches to the value obtained from the file
		// so either the variant is something else or the CRC is not applied to the whole file but on a part
		/*int recordedCRC =*/ headerBlock.ReadInt32();

		// Read the lengths, uncompress the blocks and bail if any block could not be uncompressed
		// This could look better without all the copy-paste, but meh.
		std::int32_t infoBlockPackedSize = headerBlock.ReadInt32();
		std::int32_t infoBlockUnpackedSize = headerBlock.ReadInt32();
		std::int32_t eventBlockPackedSize = headerBlock.ReadInt32();
		std::int32_t eventBlockUnpackedSize = headerBlock.ReadInt32();
		std::int32_t dictBlockPackedSize = headerBlock.ReadInt32();
		std::int32_t dictBlockUnpackedSize = headerBlock.ReadInt32();
		std::int32_t layoutBlockPackedSize = headerBlock.ReadInt32();
		std::int32_t layoutBlockUnpackedSize = headerBlock.ReadInt32();

		// The blocks follow one another in the file, each is released as soon as it's parsed, the unpacked events of a big
		// level alone take a few MB, which consoles can't spare
		{
			JJ2Block infoBlock(s, infoBlockPackedSize, infoBlockUnpackedSize);
			LoadMetadata(infoBlock, strictParser);
		}
		{
			JJ2Block eventBlock(s, eventBlockPackedSize, eventBlockUnpackedSize);
			if (!LoadEvents(eventBlock, strictParser)) {
				return false;
			}
		}
		{
			JJ2Block dictBlock(s, dictBlockPackedSize, dictBlockUnpackedSize);
			JJ2Block layoutBlock(s, layoutBlockPackedSize, layoutBlockUnpackedSize);
			if (!LoadLayers(dictBlock, layoutBlock, strictParser)) {
				return false;
			}
		}

		// Try to read MLLE data stream
		std::uint32_t mlleMagic = s->ReadValueAsLE<std::uint32_t>();
		if (mlleMagic == 0x454C4C4D /*MLLE*/) {
			std::uint32_t mlleVersion = s->ReadValueAsLE<std::uint32_t>();
			std::int32_t mlleBlockPackedSize = s->ReadValueAsLE<std::int32_t>();
			std::int32_t mlleBlockUnpackedSize = s->ReadValueAsLE<std::int32_t>();

			JJ2Block mlleBlock(s, mlleBlockPackedSize, mlleBlockUnpackedSize);
			LoadMlleData(mlleBlock, mlleVersion, path, strictParser);
			_isPlusLevel = true;
		}
		if (!_isPlusLevel) {
			// JJ2+ runs the script of the same name next to the level
			StringView foundDot = path.findLastOr('.', path.end());
			_isPlusLevel = fs::IsReadableFile(fs::FindPathCaseInsensitive(path.prefix(foundDot.begin()) + ".j2as"_s));
		}

		return true;
	}

	void JJ2Level::LoadMetadata(JJ2Block& block, bool strictParser)
	{
		// First 9 bytes are JCS coordinates on last save.
		block.DiscardBytes(9);

		LightingMin = block.ReadByte();
		LightingStart = block.ReadByte();

		_animCount = block.ReadUInt16();
		if (_animCount > GetMaxSupportedTiles()) {
			LOGW("Level \"{}\" declares {} animated tiles, more than the {} tiles it can have", LevelName, _animCount, GetMaxSupportedTiles());
			_animCount = std::uint16_t(GetMaxSupportedTiles());
		}

		_verticalMPSplitscreen = block.ReadBool();
		_isMpLevel = block.ReadBool();

		// This should be the same as size of block in the start?
		/*std::int32_t headerSize =*/ block.ReadInt32();

		String secondLevelName = block.ReadString(32, true);
		//DEATH_ASSERT(!strictParser || DisplayName == secondLevelName, "Level name mismatch", );

		Tileset = block.ReadString(32, true);
		BonusLevel = block.ReadString(32, true);
		NextLevel = block.ReadString(32, true);
		SecretLevel = block.ReadString(32, true);
		Music = block.ReadString(32, true);

		for (std::int32_t i = 0; i < TextEventStringsCount; ++i) {
			_textEventStrings[i] = block.ReadString(512, true);
		}

		LoadLayerMetadata(block, strictParser);

		/*uint16_t staticTilesCount =*/ block.ReadUInt16();
		//DEATH_ASSERT(!strictParser || GetMaxSupportedTiles() - _animCount == staticTilesCount, "Tile count mismatch", );

		LoadStaticTileData(block, strictParser);

		// The unused XMask field
		block.DiscardBytes(GetMaxSupportedTiles());

		LoadAnimatedTiles(block, strictParser);
	}

	void JJ2Level::LoadStaticTileData(JJ2Block& block, bool strictParser)
	{
		std::int32_t tileCount = GetMaxSupportedTiles();
		_staticTiles = std::make_unique<TilePropertiesSection[]>(tileCount);

		for (std::int32_t i = 0; i < tileCount; ++i) {
			std::uint32_t tileEvent = block.ReadUInt32();

			auto& tile = _staticTiles[i];
			tile.Event.EventType = (JJ2Event)(std::uint8_t)(tileEvent & 0x000000FF);
			tile.Event.Difficulty = (std::uint8_t)((tileEvent & 0x0000C000) >> 14);
			tile.Event.Illuminate = ((tileEvent & 0x00002000) >> 13 == 1);
			tile.Event.TileParams = (std::uint32_t)(((tileEvent >> 12) & 0x000FFFF0) | ((tileEvent >> 8) & 0x0000000F));
		}
		for (std::int32_t i = 0; i < tileCount; ++i) {
			_staticTiles[i].Flipped = block.ReadBool();
		}

		for (std::int32_t i = 0; i < tileCount; ++i) {
			_staticTiles[i].Type = block.ReadByte();
		}
	}

	void JJ2Level::LoadAnimatedTiles(JJ2Block& block, bool strictParser)
	{
		_animatedTiles = std::make_unique<AnimatedTileSection[]>(_animCount);

		for (std::int32_t i = 0; i < _animCount; i++) {
			auto& tile = _animatedTiles[i];
			tile.Delay = block.ReadUInt16();
			tile.DelayJitter = block.ReadUInt16();
			tile.ReverseDelay = block.ReadUInt16();
			tile.IsPingPong = block.ReadBool();
			tile.Speed = block.ReadByte(); // 0-70
			// The frame list that follows is always MaxAnimationFrames entries long, but the count in front
			// of it is a byte and nothing in the format stops it naming more. Convert() walks the frames up
			// to this count, so a file saying 255 would have it read past the end of Frames - into the next
			// tile of the array, and for the LAST tile past the end of the allocation entirely.
			tile.FrameCount = block.ReadByte();
			if (tile.FrameCount > MaxAnimationFrames) {
				LOGW("Animated tile {} of level \"{}\" declares {} frames, more than the {} the format stores",
					i, LevelName, tile.FrameCount, MaxAnimationFrames);
				tile.FrameCount = MaxAnimationFrames;
			}

			for (std::int32_t j = 0; j < MaxAnimationFrames; j++) {
				tile.Frames[j] = block.ReadUInt16();
			}
		}
	}

	void JJ2Level::LoadLayerMetadata(JJ2Block& block, bool strictParser)
	{
		_layers.resize(JJ2LayerCount);

		for (std::int32_t i = 0; i < JJ2LayerCount; i++) {
			_layers[i].Flags = block.ReadUInt32();
		}

		for (std::int32_t i = 0; i < JJ2LayerCount; i++) {
			_layers[i].Type = block.ReadByte();
		}

		for (std::int32_t i = 0; i < JJ2LayerCount; i++) {
			_layers[i].Used = block.ReadBool();
			_layers[i].Visible = true;
		}

		for (std::int32_t i = 0; i < JJ2LayerCount; i++) {
			_layers[i].Width = block.ReadInt32();
		}

		// This is related to how data is presented in the file, the above is a WYSIWYG version, solely shown on the UI
		for (std::int32_t i = 0; i < JJ2LayerCount; i++) {
			_layers[i].InternalWidth = block.ReadInt32();
		}

		for (std::int32_t i = 0; i < JJ2LayerCount; i++) {
			_layers[i].Height = block.ReadInt32();
		}

		for (std::int32_t i = 0; i < JJ2LayerCount; i++) {
			_layers[i].Depth = block.ReadInt32();
		}

		for (std::int32_t i = 0; i < JJ2LayerCount; i++) {
			_layers[i].DetailLevel = block.ReadByte();
		}

		for (std::int32_t i = 0; i < JJ2LayerCount; i++) {
			_layers[i].OffsetX = block.ReadFloatEncoded();
		}

		for (std::int32_t i = 0; i < JJ2LayerCount; i++) {
			_layers[i].OffsetY = block.ReadFloatEncoded();
		}

		for (std::int32_t i = 0; i < JJ2LayerCount; i++) {
			_layers[i].SpeedX = block.ReadFloatEncoded();
		}

		for (std::int32_t i = 0; i < JJ2LayerCount; i++) {
			_layers[i].SpeedY = block.ReadFloatEncoded();
		}

		// The speed models are the defaults of JJ2+ unless the MLLE stream has its own, layer 8 is tied to the screen
		for (std::int32_t i = 0; i < JJ2LayerCount; i++) {
			_layers[i].AutoSpeedX = block.ReadFloatEncoded();
			_layers[i].SpeedModelX = (i == 7 ? LayerSectionSpeedModel::Legacy : LayerSectionSpeedModel::Normal);
		}

		for (std::int32_t i = 0; i < JJ2LayerCount; i++) {
			_layers[i].AutoSpeedY = block.ReadFloatEncoded();
			_layers[i].SpeedModelY = (i == 7 ? LayerSectionSpeedModel::Legacy : LayerSectionSpeedModel::Normal);
		}

		for (std::int32_t i = 0; i < JJ2LayerCount; i++) {
			_layers[i].TexturedBackgroundType = block.ReadByte();
		}

		for (std::int32_t i = 0; i < JJ2LayerCount; i++) {
			_layers[i].TexturedParams1 = block.ReadByte();
			_layers[i].TexturedParams2 = block.ReadByte();
			_layers[i].TexturedParams3 = block.ReadByte();

			_layers[i].SpriteMode = 0;
			_layers[i].SpriteParam = 0;
		}

		for (std::int32_t i = 0; i < JJ2LayerCount; i++) {
			auto& layer = _layers[i];
			if (layer.InternalWidth < 0 || layer.Height < 0 || layer.Width < 0 || layer.Width > layer.InternalWidth) {
				LOGW("Layer {} of level \"{}\" has inconsistent size {}x{} (stride {})", i, LevelName, layer.Width, layer.Height, layer.InternalWidth);
				layer.InternalWidth = std::max<std::int32_t>(layer.InternalWidth, 0);
				layer.Height = std::max<std::int32_t>(layer.Height, 0);
				layer.Width = std::clamp<std::int32_t>(layer.Width, 0, layer.InternalWidth);
			}
		}
	}

	bool JJ2Level::LoadEvents(JJ2Block& block, bool strictParser)
	{
		std::int32_t width = _layers[3].Width;
		std::int32_t height = _layers[3].Height;
		if (width <= 0 || height <= 0) {
			return true;
		}

		if (std::int64_t(width) * height > block.GetLength() / 4) {
			LOGE("Event map of level \"{}\" ({}x{}) does not fit its block of {} bytes", LevelName, width, height, block.GetLength());
			return false;
		}

		_events.clear();

		const std::int32_t count = width * height;
		JJ2Event lastEventType = JJ2Event::EMPTY;
		for (std::int32_t i = 0; i < count; i++) {
			std::uint32_t eventData = block.ReadUInt32();
			if (eventData == 0) {
				lastEventType = JJ2Event::EMPTY;
				continue;
			}

			auto& gridEvent = _events.emplace_back();
			gridEvent.Index = i;
			auto& tileEvent = gridEvent.Event;
			tileEvent.EventType = (JJ2Event)(std::uint8_t)(eventData & 0x000000FF);
			tileEvent.Difficulty = (std::uint8_t)((eventData & 0x00000300) >> 8);
			tileEvent.Illuminate = ((eventData & 0x00000400) >> 10 == 1);
			tileEvent.TileParams = ((eventData & 0xFFFFF000) >> 12);
			lastEventType = tileEvent.EventType;

			if (tileEvent.EventType == JJ2Event::CTF_BASE) {
				_hasCTF = true;
			} else if (tileEvent.EventType == JJ2Event::WARP_ORIGIN) {
				if (((tileEvent.TileParams >> 16) & 1) == 1) {
					_hasLaps = true;
				}
			}
		}

		if (lastEventType == JJ2Event::MODIFIER_ONE_WAY) {
			_hasPit = false;
			_hasPitInstantDeath = false;
		} else if (lastEventType == JJ2Event::MCE) {
			_hasPit = true;
			_hasPitInstantDeath = true;
		} else {
			_hasPit = true;
			_hasPitInstantDeath = false;
		}
		return true;
	}

	const JJ2Level::TileEventSection* JJ2Level::FindEvent(std::int32_t x, std::int32_t y) const
	{
		const std::int32_t index = x + y * _layers[3].Width;
		auto it = std::lower_bound(_events.begin(), _events.end(), index, [](const GridEventSection& gridEvent, std::int32_t index) {
			return gridEvent.Index < index;
		});
		return (it != _events.end() && it->Index == index ? &it->Event : nullptr);
	}

	bool JJ2Level::LoadLayers(JJ2Block& dictBlock, JJ2Block& layoutBlock, bool strictParser)
	{
		struct DictionaryEntry {
			std::uint16_t Tiles[4];
		};

		const std::int32_t dictLength = dictBlock.GetLength() / std::int32_t(sizeof(DictionaryEntry));
		std::unique_ptr<DictionaryEntry[]> dictionary = std::make_unique<DictionaryEntry[]>(dictLength);
		for (std::int32_t i = 0; i < dictLength; i++) {
			auto& entry = dictionary[i];
			for (std::int32_t j = 0; j < 4; j++) {
				entry.Tiles[j] = dictBlock.ReadUInt16();
			}
		}

		std::int64_t layoutEntries = 0;
		for (std::int32_t i = 0; i < JJ2LayerCount; i++) {
			const auto& layer = _layers[i];
			if (layer.Used) {
				layoutEntries += std::int64_t(layer.Height) * ((std::int64_t(layer.InternalWidth) + 3) / 4);
			}
		}
		if (layoutEntries > layoutBlock.GetLength() / 2) {
			LOGE("Layers of level \"{}\" need {} layout entries, but its block holds only {}", LevelName, layoutEntries, layoutBlock.GetLength() / 2);
			return false;
		}

		std::int32_t invalidEntries = 0;
		for (std::int32_t i = 0; i < JJ2LayerCount; i++) {
			auto& layer = _layers[i];

			if (layer.Used && layer.InternalWidth > 0 && layer.Height > 0) {
				layer.Tiles = std::make_unique<std::uint16_t[]>(layer.InternalWidth * layer.Height);

				for (std::int32_t y = 0; y < layer.Height; y++) {
					for (std::int32_t x = 0; x < layer.InternalWidth; x += 4) {
						std::uint16_t dictIdx = layoutBlock.ReadUInt16();
						if (dictIdx >= dictLength) {
							invalidEntries++;
							continue;
						}

						for (std::int32_t j = 0; j < 4; j++) {
							if (j + x >= layer.Width) {
								break;
							}

							layer.Tiles[j + x + y * layer.InternalWidth] = dictionary[dictIdx].Tiles[j];
						}
					}
				}
			}
		}

		if (invalidEntries > 0) {
			LOGW("Level \"{}\" references {} dictionary entries past the {} it has, these tiles were left empty", LevelName, invalidEntries, dictLength);
		}
		return true;
	}

	void JJ2Level::LoadMlleData(JJ2Block& block, std::uint32_t version, StringView path, bool strictParser)
	{
		if (version > 0x107) {
			LOGW("MLLE stream version 0x{:x} in level \"{}\" is not supported", version, LevelName);
			return;
		}

		bool isSnowing = block.ReadBool();
		bool isSnowingOutdoorsOnly = block.ReadBool();
		std::uint8_t snowIntensity = block.ReadByte();
		// Weather particles type (Snow, Flower, Rain, Leaf)
		std::uint8_t snowType = block.ReadByte();

		if (isSnowing) {
			_weatherIntensity = snowIntensity;
			_weatherType = (WeatherType)(snowType + 1);
			if (_weatherType != WeatherType::None && isSnowingOutdoorsOnly) {
				_weatherType |= WeatherType::OutdoorsOnly;
			}
		}

		// TODO: Convert remaining coins to gems when warp is used
		bool warpsTransmuteCoins = block.ReadBool();
		// TODO: Objects spawned from Generators will derive their parameters from tile they first appear at (e.g., after gravitation)
		bool delayGeneratedCrateOrigins = block.ReadBool();
		std::int32_t echo = block.ReadInt32();
		std::uint32_t darknessColorBgra = block.ReadUInt32();
		_darknessColor = ((darknessColorBgra >> 16) & 0xff) | (darknessColorBgra & 0x0000ff00) | ((darknessColorBgra << 16) & 0x00ff0000);
		// TODO: Water level change speed
		float waterChangeSpeed = ReadFiniteFloat(block);
		// TODO: How player should react to being underwater (PositionBased, Swim, LowGravity)
		std::uint8_t waterInteraction = block.ReadByte();
		std::int32_t waterLayer = block.ReadInt32();
		// TODO: How water and ambient lighting should interact in the level (None, Global, Lagunicus)
		std::uint8_t waterLighting = block.ReadByte();
		// Clamped first, a float outside of the range of the integer it's converted to is undefined behavior
		_waterLevel = (std::uint16_t)std::clamp(ReadFiniteFloat(block), 0.0f, 65535.0f);
		std::uint32_t waterGradientStart = block.ReadUInt32();
		std::uint32_t waterGradientStop = block.ReadUInt32();

		// Level palette
		_useLevelPalette = block.ReadBool();
		if (_useLevelPalette) {
			block.ReadRawBytes(_levelPalette, sizeof(_levelPalette));

			if (version >= 0x106) {
				// TODO: Reapply palette on death
				bool reapplyPaletteOnDeath = block.ReadBool();
			}
		}

		// Additional palettes for SPRITE::MAPPING and 24-bit tile sets
		if (version >= 0x106) {
			std::uint8_t extraPaletteCount = block.ReadByte();
			while (extraPaletteCount-- != 0) {
				auto& palette = AlternatePalettes.emplace_back();
				std::int32_t nameLength = std::clamp<std::int32_t>(block.ReadUint7bitEncoded(), 0, block.GetRemainingLength());
				palette.Name = String(NoInit, nameLength);
				block.ReadRawBytes((std::uint8_t*)palette.Name.data(), nameLength);
				block.ReadRawBytes(palette.Colors, sizeof(palette.Colors));
			}
		}

		// TODO: Recolorable sprites
		std::int32_t recolorableSpriteListSize = (version >= 0x105 ? 20 : 11);
		for (std::int32_t i = 0; i < recolorableSpriteListSize; ++i) {
			// NOTE: Recolorable sprite list was expanded in MLLE-Include-1.5
			if (block.ReadBool()) {
				block.DiscardBytes(256);
			}
		}

		// Extra tilesets
		std::int32_t extraTilesetCount = block.ReadByte();
		for (std::int32_t i = 0; i < extraTilesetCount; i++) {
			auto& tileset = ExtraTilesets.emplace_back();

			std::int32_t tilesetNameLength = block.ReadUint7bitEncoded();
			tileset.Name = block.ReadString(tilesetNameLength, false);

			tileset.Offset = block.ReadUInt16();
			tileset.Count = block.ReadUInt16();
			tileset.ColorMode = (TilesetColorMode)block.ReadByte();
			switch (tileset.ColorMode) {
				case TilesetColorMode::Original8bit:
				case TilesetColorMode::Original24bit:
					// No additional parameters
					break;
				case TilesetColorMode::Remapped8bit:
					block.ReadRawBytes(tileset.PaletteRemapping, sizeof(tileset.PaletteRemapping));
					break;
				case TilesetColorMode::AlternatePalette24bit:
					tileset.AlternatePaletteMappingID24Bit = block.ReadByte();
					break;
			}
		}

		// Additional layers
		if (version >= 0x102) {
			std::int32_t layerCount = block.ReadInt32();
			if (layerCount < JJ2LayerCount || layerCount > MaxMlleLayerCount) {
				LOGW("Level \"{}\" declares {} layers in its MLLE stream, ignoring the rest of the stream", LevelName, layerCount);
				return;
			}

			for (std::int32_t i = 8; i < layerCount; i += 8) {
				char numberBuffer[16];
				i32tos(i / 8, numberBuffer);

				StringView foundDot = path.findLastOr('.', path.end());
				String extraLayersPath = fs::FindPathCaseInsensitive(path.prefix(foundDot.begin()) + "-MLLE-Data-"_s + numberBuffer + ".j2l"_s);

				JJ2Level extraLayersFile;
				if (extraLayersFile.Open(extraLayersPath, strictParser)) {
					for (std::int32_t j = 0; j < 8 && (i + j) < layerCount; j++) {
						auto& extraLayer = _layers.emplace_back(Death::move(extraLayersFile._layers[j]));
						// Only layer 8 of the level itself is tied to the screen unless said otherwise, as in JJ2+
						extraLayer.SpeedModelX = LayerSectionSpeedModel::Normal;
						extraLayer.SpeedModelY = LayerSectionSpeedModel::Normal;
					}
				} else {
					for (std::int32_t j = 0; j < 8 && (i + j) < layerCount; j++) {
						_layers.emplace_back();
					}
				}
			}

			SmallVector<std::int32_t, JJ2LayerCount * 2> layerOrder(layerCount);
			std::int32_t nextExtraLayerIdx = JJ2LayerCount;
			for (std::int32_t i = 0; i < layerCount; i++) {
				std::int8_t id = (std::int8_t)block.ReadByte();
				std::int32_t idx;
				if (id >= 0) {
					idx = id;
				} else {
					idx = nextExtraLayerIdx++;
				}
				if (idx >= (std::int32_t)_layers.size()) {
					LOGW("Level \"{}\" references layer {} of {} in its MLLE stream, ignoring the rest of the stream", LevelName, idx, _layers.size());
					return;
				}
				auto& layer = _layers[idx];
				layerOrder[idx] = i;

				std::int32_t layerNameLength = block.ReadUint7bitEncoded();
				layer.Name = block.ReadString(layerNameLength, false);

				layer.Visible = !block.ReadBool();
				layer.SpriteMode = block.ReadByte();
				layer.SpriteParam = block.ReadByte();
				std::int32_t rotationAngle = block.ReadInt32();
				std::int32_t rotationRadiusMult = block.ReadInt32();

				if (version >= 0x106) {
					layer.SpeedModelX = (LayerSectionSpeedModel)block.ReadByte();
					layer.SpeedModelY = (LayerSectionSpeedModel)block.ReadByte();
					// Texture surface (Untextured, Legacy, Fullscreen, InnerWindow, InnerLayer), then the fade - or the tint
					// color of a reflection - and the fade position - the horizontal one and the top of a reflection
					layer.TextureSurface = block.ReadByte();
					layer.TextureFade = block.ReadByte();
					layer.TextureFadeX = ReadFiniteFloat(block);
					layer.TextureFadeY = ReadFiniteFloat(block);
					// How the texture moves inside the layer drawn only where it is, as the speeds of the layer move it otherwise
					layer.InnerSpeedX = ReadFiniteFloat(block);
					layer.InnerSpeedY = ReadFiniteFloat(block);
					layer.InnerAutoSpeedX = ReadFiniteFloat(block);
					layer.InnerAutoSpeedY = ReadFiniteFloat(block);

					// A texture of JJ2+ by its number, or an image of its own, 256x256 palette indices, which follow
					layer.TextureSource = (std::int8_t)block.ReadByte();
					if (layer.TextureSource < 0) {
						constexpr std::int32_t TextureBytes = Formats::TexturedLayerChunk::TextureSize * Formats::TexturedLayerChunk::TextureSize;
						layer.TextureImage = std::make_unique<std::uint8_t[]>(TextureBytes);
						block.ReadRawBytes(layer.TextureImage.get(), TextureBytes);
					}
				}
			}

			// Sprite layer has zero depth
			std::int32_t zeroDepthIdx = layerOrder[3];

			// Adjust depth of all layers
			for (std::size_t i = 0; i < _layers.size(); i++) {
				std::int32_t newIdx = layerOrder[i];

				auto& layer = _layers[i];
				layer.Depth = (newIdx - zeroDepthIdx) * 100;
				if (layer.Depth < -200) {
					layer.Depth = -200 - (200 - layer.Depth) / 20;
				} else if (layer.Depth > 300) {
					layer.Depth = 300 + (layer.Depth - 300) / 20;
				}
			}

			// Edited tiles were added in MLLE-Include-1.3
			if (version >= 0x103) {
				std::int16_t editedTilesCount = block.ReadInt16();
				for (std::int32_t i = 0; i < editedTilesCount && !block.ReachedEndOfStream(); i++) {
					auto& tile = _overridenTileDiffuses.emplace_back();
					tile.TileID = block.ReadInt16();
					block.ReadRawBytes(tile.Diffuse, 32 * 32);
				}

				editedTilesCount = block.ReadInt16();
				for (std::int32_t i = 0; i < editedTilesCount && !block.ReachedEndOfStream(); i++) {
					auto& tile = _overridenTileMasks.emplace_back();
					tile.TileID = block.ReadInt16();
					block.ReadRawBytes(tile.Mask, 32 * 32);
				}
			}

			// TODO: Weapons were added in MLLE-Include-1.5(w)
			if (version >= 0x105) {
				// TODO: DefaultWeaponHook constructor argument was added in MLLE-Include-1.6(w)
				if (version >= 0x106) {
					std::int16_t weaponHookPrefix = block.ReadInt16();
				}
				for (std::int32_t weaponId = 0; weaponId < 9; weaponId++) {
					bool customWeapon = block.ReadBool();
					std::int32_t maximum = block.ReadInt32();

					// comesFromBirds, comesFromBirdsPowerup, comesFromGunCrates, gemsLost, gemsLostPowerup, infinite, replenishes
					constexpr std::int32_t NumberOfCommonOptions = 5;
					for (std::int32_t optionId = 0; optionId < NumberOfCommonOptions; optionId++) {
						std::uint8_t optionValue = block.ReadByte();
					}

					if (weaponId >= 6) {
						std::uint8_t ammoCrate = block.ReadByte();
					}

					if (customWeapon) {
						std::int32_t weaponNameLength = std::clamp<std::int32_t>(block.ReadUint7bitEncoded(), 0, block.GetRemainingLength());
						String weaponName{NoInit, (std::size_t)weaponNameLength};
						block.ReadRawBytes((std::uint8_t*)weaponName.data(), weaponNameLength);

						std::int32_t weaponParamsLength = block.ReadInt32();
						block.DiscardBytes(weaponParamsLength);
					} else if (weaponId == 7) {
						std::uint8_t gun8Style = block.ReadByte(); // Pepper style
					}
				}
			}

			// Off-grid objects were added in MLLE-Include-1.6
			if (version >= 0x106) {
				std::int16_t objectCount = block.ReadInt16();
				for (std::int32_t i = 0; i < objectCount; i++) {
					std::int16_t x = block.ReadInt16();
					std::int16_t y = block.ReadInt16();
					std::uint32_t eventData = block.ReadUInt32();

					auto& offGridEvent = _offGridEvents.emplace_back();
					offGridEvent.X = x;
					offGridEvent.Y = y;
					offGridEvent.EventType = (JJ2Event)(std::uint8_t)(eventData & 0x000000FF);
					offGridEvent.Difficulty = (std::uint8_t)((eventData & 0x00000300) >> 8);
					offGridEvent.Illuminate = ((eventData & 0x00000400) >> 10 == 1);
					offGridEvent.TileParams = ((eventData & 0xFFFFF000) >> 12);
				}
			}
		}
	}

	void JJ2Level::Convert(StringView targetPath, EventConverter& eventConverter, Function<LevelToken(StringView)>&& levelTokenConversion,
		Function<std::shared_ptr<const Formats::TileSetData>(StringView, ArrayView<const std::int32_t>, std::int32_t&)>&& tilesetLookup,
		Function<bool(std::int32_t, std::uint8_t*)>&& textureLookup)
	{
		auto so = fs::Open(targetPath, FileAccess::Write);
		DEATH_ASSERT(so->IsValid(), "Cannot open file for writing", );

		// Preprocess events first, so they can change some level properties
		bool hasMultiplayerSpawnPoints = false;
		for (auto& gridEvent : _events) {
			auto& tileEvent = gridEvent.Event;

			JJ2Event eventType;
			if (tileEvent.EventType == JJ2Event::MODIFIER_GENERATOR) {
				// Generators are converted differently
				std::uint8_t eventParams[16];
				EventConverter::ConvertParamInt(tileEvent.TileParams, {
					{ JJ2ParamUInt, 8 },	// Event
					{ JJ2ParamUInt, 8 },	// Delay
					{ JJ2ParamBool, 1 }		// Initial Delay
				}, eventParams);

				eventType = (JJ2Event)eventParams[0];
				tileEvent.GeneratorDelay = eventParams[1];
				tileEvent.GeneratorFlags = (std::uint8_t)eventParams[2];
			} else {
				eventType = tileEvent.EventType;
				tileEvent.GeneratorDelay = -1;
				tileEvent.GeneratorFlags = 0;

				if (tileEvent.EventType == JJ2Event::MP_LEVEL_START) {
					hasMultiplayerSpawnPoints = true;
				}
			}

			tileEvent.Converted = eventConverter.TryConvert(this, eventType, tileEvent.TileParams);
		}

		// What all the tiles without an event are converted to
		TileEventSection emptyEvent = {};
		emptyEvent.EventType = JJ2Event::EMPTY;
		emptyEvent.GeneratorDelay = -1;
		emptyEvent.Converted = eventConverter.TryConvert(this, JJ2Event::EMPTY, 0);

		for (std::size_t i = 0; i < _offGridEvents.size(); i++) {
			auto& offGridEvent = _offGridEvents[i];

			JJ2Event eventType;
			if (offGridEvent.EventType == JJ2Event::MODIFIER_GENERATOR) {
				// Generators are converted differently
				std::uint8_t eventParams[16];
				EventConverter::ConvertParamInt(offGridEvent.TileParams, {
					{ JJ2ParamUInt, 8 },	// Event
					{ JJ2ParamUInt, 8 },	// Delay
					{ JJ2ParamBool, 1 }		// Initial Delay
				}, eventParams);

				eventType = (JJ2Event)eventParams[0];
				offGridEvent.GeneratorDelay = eventParams[1];
				offGridEvent.GeneratorFlags = (std::uint8_t)eventParams[2];
			} else {
				eventType = offGridEvent.EventType;
				offGridEvent.GeneratorDelay = -1;
				offGridEvent.GeneratorFlags = 0;
			}

			offGridEvent.Converted = eventConverter.TryConvert(this, eventType, offGridEvent.TileParams);
		}

		CheckWaterLevelAroundStart();

		// Flags
		LevelFlags flags = LevelFlags::None;
		if (_hasPit) {
			flags |= LevelFlags::HasPit;
		}
		if (_hasPitInstantDeath) {
			flags |= LevelFlags::HasPitInstantDeath;
		}
		if (_useLevelPalette) {
			flags |= LevelFlags::UseLevelPalette;
		}
		if (_isHidden) {
			flags |= LevelFlags::IsHidden;
		}
		if (_isMpLevel) {
			flags |= LevelFlags::IsMultiplayerLevel;
			if (_hasLaps) {
				flags |= LevelFlags::HasLaps;
			}
			if (_hasCTF) {
				flags |= LevelFlags::HasCaptureTheFlag;
			}
		}
		if (_verticalMPSplitscreen) {
			flags |= LevelFlags::HasVerticalSplitscreen;
		}
		if (hasMultiplayerSpawnPoints) {
			flags |= LevelFlags::HasMultiplayerSpawnPoints;
		}
		Formats::LevelWriter writer(*so, flags);

		Formats::LevelProperties properties;
		properties.DisplayName = JJ2Strings::RecodeString(DisplayName, true);

		StringUtils::lowercaseInPlace(NextLevel);
		StringUtils::lowercaseInPlace(SecretLevel);
		StringUtils::lowercaseInPlace(BonusLevel);

		properties.NextLevel = ConvertLevelName(NextLevel, levelTokenConversion);
		properties.SecretLevel = ConvertLevelName(SecretLevel, levelTokenConversion);
		properties.BonusLevel = ConvertLevelName(BonusLevel, levelTokenConversion);

		// Default Tileset
		StringUtils::lowercaseInPlace(Tileset);
		if (Tileset.hasSuffix(".j2t"_s)) {
			Tileset = Tileset.exceptSuffix(".j2t"_s);
		}
		if (LevelName == "arace2"_s && Tileset == "hauntedh"_s) {
			// arace2.j2l uses hauntedh.j2t, but this file doesn't exist in some distributions
			Tileset = "hauntedh1"_s;
		}
		properties.DefaultTileSet = Tileset;

		// Default Music
		StringUtils::lowercaseInPlace(Music);
		if (Music.find('.') == nullptr) {
			String music = Music + ".j2b"_s;
			properties.Music = Death::move(music);
		} else {
			properties.Music = Music;
		}

		// Alpha is the starting light intensity
		properties.AmbientColor = (_darknessColor & 0x00FFFFFF) | (std::uint32_t(std::min(LightingStart * 255 / 64, 255)) << 24);
		properties.Weather = std::uint8_t(_weatherType);
		properties.WeatherIntensity = _weatherIntensity;
		properties.WaterLevel = _waterLevel;

		// Find caption tile
		std::uint16_t maxTiles = std::uint16_t(GetMaxSupportedTiles());

		properties.CaptionTileId = 0;
		for (std::uint16_t i = 0; i < maxTiles; i++) {
			if (_staticTiles[i].Type == 4) {
				properties.CaptionTileId = i;
				break;
			}
		}

		writer.WriteProperties(properties);

		// Custom palettes
		std::uint32_t levelPalette[Formats::LevelFile::ColorsPerPalette];
		if (_useLevelPalette) {
			ExpandPalette(_levelPalette, levelPalette);
		}

		SmallVector<Formats::PaletteRecord, 0> additionalPalettes;
		additionalPalettes.reserve(AlternatePalettes.size());
		for (const auto& palette : AlternatePalettes) {
			auto& record = additionalPalettes.emplace_back();
			record.Name = palette.Name;
			ExpandPalette(palette.Colors, record.Colors);
		}

		writer.WritePalettes(_useLevelPalette ? levelPalette : nullptr, additionalPalettes);

		// Extra Tilesets
		SmallVector<Formats::ExtraTileSetRecord, 0> extraTileSets;
		extraTileSets.reserve(ExtraTilesets.size());
		for (auto& tileset : ExtraTilesets) {
			auto& record = extraTileSets.emplace_back();
			record.Flags = 0;
			if (tileset.ColorMode == TilesetColorMode::Remapped8bit || tileset.ColorMode == TilesetColorMode::AlternatePalette24bit) {
				record.Flags |= Formats::ExtraTileSetRecord::Remapped;
			}
			if (tileset.ColorMode == TilesetColorMode::Original24bit || tileset.ColorMode == TilesetColorMode::AlternatePalette24bit) {
				record.Flags |= Formats::ExtraTileSetRecord::TrueColor;
			}

			StringUtils::lowercaseInPlace(tileset.Name);
			if (tileset.Name.hasSuffix(".j2t"_s)) {
				tileset.Name = tileset.Name.exceptSuffix(".j2t"_s);
			}

			record.Name = tileset.Name;
			record.Offset = tileset.Offset;
			record.Count = tileset.Count;
			std::memset(record.Remapping, 0, sizeof(record.Remapping));
			record.AlternatePalette = 0;
			if (tileset.ColorMode == TilesetColorMode::Remapped8bit) {
				std::memcpy(record.Remapping, tileset.PaletteRemapping, sizeof(record.Remapping));
			} else if (tileset.ColorMode == TilesetColorMode::AlternatePalette24bit) {
				record.AlternatePalette = tileset.AlternatePaletteMappingID24Bit;
			}
		}

		writer.WriteExtraTileSets(extraTileSets);

		// Overriden tiles
		SmallVector<Formats::TileDiffuseOverride, 0> diffuseOverrides;
		diffuseOverrides.reserve(_overridenTileDiffuses.size());
		for (const auto& tile : _overridenTileDiffuses) {
			auto& record = diffuseOverrides.emplace_back();
			record.TileId = tile.TileID;
			std::memcpy(record.Indices, tile.Diffuse, sizeof(record.Indices));
		}
		writer.WriteTileDiffuseOverrides(diffuseOverrides);

		SmallVector<Formats::TileMaskOverride, 0> maskOverrides;
		maskOverrides.reserve(_overridenTileMasks.size());
		for (const auto& tile : _overridenTileMasks) {
			auto& record = maskOverrides.emplace_back();
			record.TileId = tile.TileID;
			std::memcpy(record.Mask, tile.Mask, sizeof(record.Mask));
		}
		writer.WriteTileMaskOverrides(maskOverrides);

		// Text Event Strings
		SmallVector<String, TextEventStringsCount> texts;
		for (std::int32_t i = 0; i < TextEventStringsCount; i++) {
			String& text = _textEventStrings[i];

			bool isLevelToken = false;
			for (std::uint8_t textId : _levelTokenTextIds) {
				if (i == textId) {
					isLevelToken = true;
					break;
				}
			}

			if (isLevelToken) {
				String adjustedText;
				auto levelTokens = text.split('|');
				for (std::int32_t j = 0; j < (std::int32_t)levelTokens.size(); j++) {
					if (j != 0) {
						adjustedText += "|"_s;
					}
					StringUtils::lowercaseInPlace(levelTokens[j]);
					LevelToken token = levelTokenConversion(levelTokens[j]);
					if (!token.Episode.empty()) {
						adjustedText += token.Episode + "/"_s;
					}
					adjustedText += token.Level;
				}
				texts.push_back(Death::move(adjustedText));
			} else {
				texts.push_back(JJ2Strings::RecodeString(text));
			}
		}
		writer.WriteTexts(texts);

		// Animated Tiles
		std::uint16_t lastTilesetTileIndex = std::uint16_t(maxTiles - _animCount);

		SmallVector<Formats::AnimatedTileRecord, 0> animatedTiles;
		animatedTiles.reserve(_animCount);
		for (std::int32_t i = 0; i < _animCount; i++) {
			auto& tile = _animatedTiles[i];
			auto& record = animatedTiles.emplace_back();
			record.FrameDuration16 = std::uint16_t(tile.Speed == 0 ? 0 : 16 * 50 / tile.Speed);
			record.Delay = tile.Delay;
			record.DelayJitter = tile.DelayJitter;
			record.IsPingPong = (tile.IsPingPong ? 1 : 0);
			record.PingPongDelay = tile.ReverseDelay;
			record.Frames.reserve(tile.FrameCount);

			for (std::int32_t j = 0; j < tile.FrameCount; j++) {
				// Max. tiles is either 0x0400 or 0x1000 and doubles as a mask to separate flipped tiles.
				// In J2L, each flipped tile had a separate entry in the tile list, probably to make
				// the dictionary concept easier to handle.
				bool flipX = false, flipY = false;
				std::uint16_t tileIdx = tile.Frames[j];
				if ((tileIdx & maxTiles) != 0) {
					flipX = true;
					tileIdx &= ~maxTiles;
				}
				if ((tileIdx & 0x2000) != 0) {
					flipY = true;
					tileIdx &= ~0x2000;
				}

				if (tileIdx >= lastTilesetTileIndex) {
					std::uint16_t animIndex = tileIdx - lastTilesetTileIndex;
					if (animIndex < _animCount) {
						tileIdx = _animatedTiles[animIndex].Frames[0];
					} else {
						LOGE("Animated tile references undefined tile {} in level \"{}\" (max. tile count is {}, anim. count is {})", tileIdx, LevelName, maxTiles, _animCount);
						tileIdx = 0;
					}
				}

				std::uint8_t tileFlags = 0x00;
				if (flipX) {
					tileFlags |= Formats::TileRecord::FlipX;
				}
				if (flipY) {
					tileFlags |= Formats::TileRecord::FlipY;
				}

				if (tileIdx < maxTiles) {
					if (_staticTiles[tileIdx].Type == 1) {
						tileFlags |= Formats::TileRecord::ModifierTranslucent; // Legacy Translucent
					} else if (_staticTiles[tileIdx].Type == 3) {
						tileFlags |= Formats::TileRecord::ModifierInvisible;
					} else if (_staticTiles[tileIdx].Type == TileTypeHeatEffect) {
						tileFlags |= Formats::TileRecord::ModifierHeat;
					}
				}

				auto& frame = record.Frames.emplace_back();
				frame.Flags = tileFlags;
				frame.TileId = tileIdx;
			}
		}

		writer.WriteAnimatedTiles(lastTilesetTileIndex, animatedTiles);

		// Layers - every one of them, even one without tiles, which JJ2+ keeps too, only it never draws it (jjLAYER::hasTileMap),
		// scripts refer to the layers by their numbers and names; only one of no size at all is left out, no level can have it
		auto isWritten = [](const LayerSection& layer) {
			return (layer.Width > 0 && layer.Height > 0);
		};
		std::int32_t layerCount = 0;
		std::int32_t layersWithoutTiles = 0;
		std::int64_t tilesWithoutTiles = 0;
		for (std::int32_t i = 0; i < (std::int32_t)_layers.size(); i++) {
			const auto& layer = _layers[i];
			if (isWritten(layer)) {
				layerCount++;
				if (!layer.Used || layer.Tiles == nullptr) {
					layersWithoutTiles++;
					tilesWithoutTiles += std::int64_t(layer.Width) * layer.Height;
				}
			}
		}
		if (layersWithoutTiles > 0) {
			LOGD("Level \"{}\" has {} layers without tiles, {} tiles in total", LevelName, layersWithoutTiles, tilesWithoutTiles);
		}

		writer.WriteLayerCount(layerCount);
		// What scripts know the layers by, in the order they are written
		SmallVector<StringView, JJ2LayerCount> layerNames;
		SmallVector<std::uint8_t, JJ2LayerCount> layerNumbers;

		// The preview needs the converted layers whole
		const bool makePreview = bool(tilesetLookup);
		SmallVector<Formats::LevelPreview::RecordSource::Layer, JJ2LayerCount> previewLayers;
		SmallVector<SmallVector<Formats::TileRecord, 0>, JJ2LayerCount> previewTiles;

		SmallVector<Formats::TexturedLayerChunk::Entry, JJ2LayerCount> texturedLayers;
		SmallVector<std::uint8_t, 0> spriteLayerChunkData;
		std::int32_t writtenLayers = 0;

		SmallVector<Formats::TileRecord, 0> layerRow;
		for (std::int32_t i = 0; i < (std::int32_t)_layers.size(); i++) {
			auto& layer = _layers[i];
			if (!isWritten(layer)) {
				continue;
			}

			bool isSprite = (i == 3);
			bool hasTiles = (layer.Used && layer.Tiles != nullptr);

			Formats::LayerRecord record;
			// The texture of its own of a textured layer, empty if its tiles are
			SmallVector<std::uint8_t, 0> texture;
			MakeLayerRecord(i, record, texture, textureLookup, _isPlusLevel);
			layerNames.push_back(layer.Name);
			layerNumbers.push_back(record.OriginalNumber);
			if (isSprite) {
				// How the sprite layer is drawn isn't in its record, it goes into a chunk, see Formats::SpriteLayerChunk - none
				// for one drawn as ordinary tiles
				Formats::SpriteLayerChunk::Write(record, spriteLayerChunkData);
			}

			writer.WriteLayerHeader(record);
			// The parameters the record has no room for go into a chunk, by the index of the layer in the file, and the texture too
			Formats::TexturedLayerChunk::Entry& texturedLayer = texturedLayers.emplace_back();
			texturedLayer.Layer = std::uint32_t(writtenLayers);
			texturedLayer.Params = record.Textured;
			texturedLayer.Texture = Death::move(texture);
			writtenLayers++;

			SmallVector<Formats::TileRecord, 0>* previewLayerTiles = nullptr;
			if (makePreview) {
				previewLayers.push_back({ record, {} });
				previewLayerTiles = &previewTiles.emplace_back();
				previewLayerTiles->reserve(std::size_t(layer.Width) * std::size_t(layer.Height));
			}

			layerRow.resize_for_overwrite(layer.Width);
			for (std::int32_t y = 0; y < layer.Height; y++) {
				for (std::int32_t x = 0; x < layer.Width; x++) {
					layerRow[x] = ConvertLayerTile(hasTiles ? layer.Tiles[y * layer.InternalWidth + x] : 0);
				}
				writer.WriteLayerTiles(layerRow);
				if (previewLayerTiles != nullptr) {
					previewLayerTiles->append(layerRow.begin(), layerRow.end());
				}
			}
		}

		// Events
		Formats::LevelPreview::StartPicker previewStart;
		std::size_t nextEvent = 0;
		for (std::int32_t y = 0; y < _layers[3].Height; y++) {
			for (std::int32_t x = 0; x < _layers[3].Width; x++) {
				const TileEventSection* tileEventPtr = &emptyEvent;
				if (nextEvent < _events.size() && _events[nextEvent].Index == x + y * _layers[3].Width) {
					tileEventPtr = &_events[nextEvent].Event;
					nextEvent++;
				}
				const auto& tileEvent = *tileEventPtr;
				Formats::EventRecord record = MakeEventRecord(tileEvent.Converted, tileEvent.Illuminate, tileEvent.Difficulty,
					tileEvent.GeneratorFlags, tileEvent.GeneratorDelay);
				if (tileEvent.GeneratorDelay == -1) {
					// What the converted event has no room for, scripts read it (the parameters of a generator are its own)
					EventConverter::KeepOriginalParams(tileEvent.Converted.Type, record.Params, tileEvent.TileParams);
				}
				writer.WriteEvent(record);
				previewStart.Add(record, x, y);
			}
		}

		SmallVector<Formats::OffGridEventRecord, 0> offGridEvents;
		offGridEvents.reserve(_offGridEvents.size());
		for (const auto& offGridEvent : _offGridEvents) {
			auto& record = offGridEvents.emplace_back();
			record.X = offGridEvent.X;
			record.Y = offGridEvent.Y;
			record.Event = MakeEventRecord(offGridEvent.Converted, offGridEvent.Illuminate, offGridEvent.Difficulty,
				offGridEvent.GeneratorFlags, offGridEvent.GeneratorDelay);
			if (offGridEvent.GeneratorDelay == -1) {
				EventConverter::KeepOriginalParams(offGridEvent.Converted.Type, record.Event.Params, offGridEvent.TileParams);
			}
		}
		writer.WriteOffGridEvents(offGridEvents);

		SmallVector<Formats::ChunkRecord, 2> chunks;
		Formats::ChunkRecord texturedLayersChunk;
		texturedLayersChunk.Tag = Formats::TexturedLayerChunk::Tag;
		Formats::TexturedLayerChunk::Write(texturedLayers, texturedLayersChunk.Data);
		if (!texturedLayersChunk.Data.empty()) {
			chunks.push_back(Death::move(texturedLayersChunk));
		}
		if (!spriteLayerChunkData.empty()) {
			Formats::ChunkRecord& spriteLayerChunk = chunks.emplace_back();
			spriteLayerChunk.Tag = Formats::SpriteLayerChunk::Tag;
			spriteLayerChunk.Data = Death::move(spriteLayerChunkData);
		}
		Formats::ChunkRecord layerNamesChunk;
		layerNamesChunk.Tag = Formats::LayerNamesChunk::Tag;
		Formats::LayerNamesChunk::Write(layerNames, layerNamesChunk.Data);
		if (!layerNamesChunk.Data.empty()) {
			chunks.push_back(Death::move(layerNamesChunk));
		}
		Formats::ChunkRecord layerNumbersChunk;
		layerNumbersChunk.Tag = Formats::LayerNumbersChunk::Tag;
		Formats::LayerNumbersChunk::Write(layerNumbers, layerNumbersChunk.Data);
		if (!layerNumbersChunk.Data.empty()) {
			chunks.push_back(Death::move(layerNumbersChunk));
		}

		// Preview, drawn with the original tile sets, the level can't be shown without its default one
		if (makePreview) {
			for (std::size_t i = 0; i < previewLayers.size(); i++) {
				previewLayers[i].Tiles = previewTiles[i];
			}

			Formats::LevelPreview::RecordSource source;
			source.Layers = previewLayers;
			source.AnimatedTilesOffset = lastTilesetTileIndex;
			source.AnimatedTiles = animatedTiles;
			source.DiffuseOverrides = diffuseOverrides;

			// Only the tiles the preview takes are asked for, a few hundred of the thousands a tile set can have. Which tile
			// set a tile ID is of depends on the number of tiles of the default one, so the IDs are collected first
			nCine::Vector2f start = previewStart.GetPosition(_layers[3].Width, _layers[3].Height);
			SmallVector<Formats::LevelPreview::RecordSource::TileSet, 4> previewTileSets;
			previewTileSets.push_back({ nullptr, 0, INT32_MAX, nullptr, {} });
			source.TileSets = previewTileSets;
			SmallVector<std::int32_t, 0> neededIds;
			source.CollectTiles(start, [&neededIds](std::int32_t, std::int32_t index) {
				neededIds.push_back(index);
			});
			std::sort(neededIds.begin(), neededIds.end());
			neededIds.erase(std::unique(neededIds.begin(), neededIds.end()), neededIds.end());

			std::int32_t tileCount = 0;
			std::shared_ptr<const Formats::TileSetData> defaultTileSet = tilesetLookup(properties.DefaultTileSet, neededIds, tileCount);
			if (defaultTileSet != nullptr) {
				previewTileSets[0] = { defaultTileSet.get(), 0, tileCount, nullptr, neededIds };

				SmallVector<std::shared_ptr<const Formats::TileSetData>, 4> extraTileSetData;
				SmallVector<SmallVector<std::int32_t, 0>, 4> extraTiles(extraTileSets.size());
				std::int32_t firstId = tileCount;
				for (std::size_t i = 0; i < extraTileSets.size(); i++) {
					const auto& extraTileSet = extraTileSets[i];
					for (std::int32_t id : neededIds) {
						if (id >= firstId && id < firstId + extraTileSet.Count) {
							extraTiles[i].push_back(id - firstId + extraTileSet.Offset);
						}
					}
					firstId += extraTileSet.Count;

					std::int32_t extraTileCount = 0;
					const auto& data = extraTileSetData.emplace_back(tilesetLookup(extraTileSet.Name, extraTiles[i], extraTileCount));
					// A tile set of true color tiles isn't matched to the palette of the level
					constexpr std::uint8_t RemapFlags = Formats::ExtraTileSetRecord::Remapped | Formats::ExtraTileSetRecord::TrueColor;
					bool remapped = ((extraTileSet.Flags & RemapFlags) == Formats::ExtraTileSetRecord::Remapped);
					previewTileSets.push_back({ data.get(), extraTileSet.Offset, extraTileSet.Count, remapped ? extraTileSet.Remapping : nullptr, extraTiles[i] });
				}
				source.TileSets = previewTileSets;

				Formats::ChunkRecord preview;
				preview.Tag = Formats::LevelFile::PreviewChunk;
				Formats::LevelPreview::Make(source, start, _useLevelPalette ? levelPalette : defaultTileSet->Palette, preview.Data);
				if (!preview.Data.empty()) {
					chunks.push_back(Death::move(preview));
				}
			}
		}

		if (!chunks.empty()) {
			writer.WriteChunks(chunks, false);
		}

		if (!writer.Finish()) {
			LOGE("Level \"{}\" cannot be written to \"{}\"", LevelName, targetPath);
		}

#if defined(DEATH_DEBUG)
		/*auto episodeName = fs::GetFileName(fs::GetDirectoryName(targetPath));
		if (episodeName != "unknown"_s) {
			std::int32_t lastStringIdx = -1;
			for (std::int32_t i = TextEventStringsCount - 1; i >= 0; i--) {
				String& text = _textEventStrings[i];
				if (!text.empty()) {
					lastStringIdx = i;
					break;
				}
			}

			if (lastStringIdx >= 0) {
				auto so = fs::Open(targetPath + ".h"_s, FileAccessMode::Write);
				DEATH_ASSERT(so->IsValid(), "Cannot open file for writing", );

				for (std::int32_t i = 0; i <= lastStringIdx; i++) {
					String& text = _textEventStrings[i];

					bool isLevelToken = false;
					for (std::uint8_t textId : _levelTokenTextIds) {
						if (i == textId) {
							isLevelToken = true;
							break;
						}
					}

					String formattedText = JJ2Strings::RecodeString(text, false, true);
					if (isLevelToken || formattedText.empty()) {
						so->Write("// [Empty text]\n", sizeof("// [Empty text]\n") - 1);
						continue;
					}

					String levelFullName = episodeName + "/"_s + fs::GetFileNameWithoutExtension(targetPath);
					so->Write("_x(\"", sizeof("__(\"") - 1);
					so->Write(levelFullName.data(), levelFullName.size());
					so->Write("\", \"", sizeof("\", \"") - 1);
					so->Write(formattedText.data(), formattedText.size());
					so->Write("\"); // ", sizeof("\"); // ") - 1);

					char buffer[32];
					u32tos((uint32_t)i, buffer);
					so->Write(buffer, std::strlen(buffer));

					so->Write("\n", sizeof("\n") - 1);
				}
			}
		}*/
#endif
	}

	void JJ2Level::AddLevelTokenTextID(std::uint8_t textId)
	{
		_levelTokenTextIds.push_back(textId);
	}

	void JJ2Level::AddEventMusic(StringView music)
	{
		for (const String& existing : _eventMusic) {
			if (existing == music) {
				return;
			}
		}
		_eventMusic.emplace_back(music);
	}

	void JJ2Level::CheckWaterLevelAroundStart()
	{
		bool waterAround = true;
		bool waterShouldResetLighting = true;
		std::int32_t waterLevelAround = INT32_MIN;

		auto checkWaterTile = [&](std::int32_t x, std::int32_t y) {
			const TileEventSection* waterTile = FindEvent(x, y);
			if (waterTile == nullptr || waterTile->Converted.Type != EventType::ModifierSetWater || waterTile->Difficulty != 0 /*!All*/ || waterTile->Converted.Params[2] == 0 /*!Instant*/) {
				waterAround = false;
			} else {
				// Check if water level matches other tiles
				std::int32_t waterLevel = waterTile->Converted.Params[0] | (waterTile->Converted.Params[1] << 8);
				if (waterLevelAround == INT32_MIN) {
					waterLevelAround = waterLevel;
				} else if (waterLevelAround != waterLevel) {
					waterAround = false;
				}

				if (waterTile->Converted.Params[3] != 0 /* !IgnoreLighting */) {
					waterShouldResetLighting = false;
				}
			}
		};

		// Check neighbor tiles of level start events if they contain water event, then set it as initial water level instead
		const std::int32_t width = _layers[3].Width;
		for (const auto& gridEvent : _events) {
			const auto& tileEvent = gridEvent.Event;
			if (tileEvent.Converted.Type == EventType::LevelStart || tileEvent.Converted.Type == EventType::LevelStartMultiplayer) {
				std::int32_t x = gridEvent.Index % width;
				std::int32_t y = gridEvent.Index / width;
				std::int32_t y2 = std::min(y + 1, _layers[3].Height - 1);
				if (x - 1 >= 0) {
					checkWaterTile(x - 1, y2);
				}
				if (y2 != y) {
					checkWaterTile(x, y2);
				}
				if (x + 1 < width) {
					checkWaterTile(x + 1, y2);
				}
			}
		}

		if (waterAround && waterLevelAround != INT32_MIN) {
			// Override water level of the level
			_waterLevel = std::uint16_t(waterLevelAround);
			if (waterShouldResetLighting) {
				// Reset also ambient lighting, because the original game ignored ambient lighting if water was used
				LightingStart = 64;
			}
		}
	}

	String JJ2Level::ConvertLevelName(StringView value, Function<LevelToken(StringView)>& levelTokenConversion)
	{
		if (value.empty()) {
			return {};
		}

		StringView adjustedValue = value;
		if (StringHasSuffixIgnoreCase(adjustedValue, ".j2l"_s) ||
			StringHasSuffixIgnoreCase(adjustedValue, ".lev"_s)) {
			adjustedValue = adjustedValue.exceptSuffix(4);
		}

		if (!levelTokenConversion) {
			return String(adjustedValue);
		}

		LevelToken token = levelTokenConversion(adjustedValue);
		if (!token.Episode.empty()) {
			String fullName = token.Episode + '/' + token.Level;
			return fullName;
		}
		return Death::move(token.Level);
	}

	void JJ2Level::ExpandPalette(const std::uint8_t* colors, std::uint32_t* palette)
	{
		// Expand JJ2+ RGB palette to RGBA, the first palette entry is fixed to transparent black
		palette[0] = 0x00000000;
		for (std::int32_t i = 1; i < Formats::LevelFile::ColorsPerPalette; i++) {
			palette[i] = (std::uint32_t)colors[i * 3] | ((std::uint32_t)colors[i * 3 + 1] << 8) | ((std::uint32_t)colors[i * 3 + 2] << 16) | 0xff000000;
		}
	}

	Tiles::LayerSpeedModel JJ2Level::ConvertSpeedModel(LayerSectionSpeedModel speedModel)
	{
		// NORMAL is the default model too, without the speed of a layer that has an auto speed (see Convert())
		switch (speedModel) {
			case LayerSectionSpeedModel::Legacy: return Tiles::LayerSpeedModel::AlwaysOnTop;
			case LayerSectionSpeedModel::FromStart: return Tiles::LayerSpeedModel::FromStart;
			case LayerSectionSpeedModel::FitLevel: return Tiles::LayerSpeedModel::FitLevel;
			case LayerSectionSpeedModel::SpeedMultipliers: return Tiles::LayerSpeedModel::SpeedMultipliers;
			default: return Tiles::LayerSpeedModel::Default;
		}
	}

	bool JJ2Level::ConvertLayer(std::int32_t index, Formats::LayerRecord& record, SmallVectorImpl<Formats::TileRecord>& tiles) const
	{
		if (index < 0 || index >= std::int32_t(_layers.size()) || _layers[index].Width <= 0 || _layers[index].Height <= 0) {
			return false;
		}

		const LayerSection& layer = _layers[index];
		SmallVector<std::uint8_t, 0> texture;
		Function<bool(std::int32_t, std::uint8_t*)> textureLookup;
		// Into a level of JJ2+, whatever the level it's taken from
		MakeLayerRecord(index, record, texture, textureLookup, true);

		const bool hasTiles = (layer.Used && layer.Tiles != nullptr);
		tiles.resize_for_overwrite(std::size_t(layer.Width) * std::size_t(layer.Height));
		for (std::int32_t y = 0; y < layer.Height; y++) {
			for (std::int32_t x = 0; x < layer.Width; x++) {
				tiles[std::size_t(y) * std::size_t(layer.Width) + std::size_t(x)] = ConvertLayerTile(hasTiles ? layer.Tiles[y * layer.InternalWidth + x] : 0);
			}
		}
		return true;
	}

	void JJ2Level::MakeLayerRecord(std::int32_t index, Formats::LayerRecord& record, SmallVectorImpl<std::uint8_t>& texture,
		Function<bool(std::int32_t, std::uint8_t*)>& textureLookup, bool forPlusLevel) const
	{
		const LayerSection& layer = _layers[index];
		bool isSky = (index == 7);
		bool isSprite = (index == 3);
		bool hasTiles = (layer.Used && layer.Tiles != nullptr);

		record = {};
		record.Kind = (isSprite ? Formats::LayerKind::Sprite : (isSky ? Formats::LayerKind::Sky : Formats::LayerKind::Other));
		// RepeatX, RepeatY, UseInherentOffset are mapped 1:1
		record.Flags = (std::uint16_t)(layer.Flags & (Formats::LayerRecord::RepeatX | Formats::LayerRecord::RepeatY | Formats::LayerRecord::UseInherentOffset));
		// A layer without tiles isn't drawn, as in JJ2+ (jjLAYER::hasTiles is false for it unless a script says otherwise)
		if (layer.Visible && (hasTiles || isSprite)) {
			record.Flags |= Formats::LayerRecord::Visible;
		}
		// The first eight are the layers of the original, the rest the additional layers of MLLE
		record.OriginalNumber = std::uint8_t(index < JJ2LayerCount ? index + 1 : 0);
		record.Width = layer.Width;
		record.Height = layer.Height;

		if (isSprite) {
			ConvertSpriteMode(layer.SpriteMode, layer.SpriteParam, record);
			return;
		}

		bool hasTexturedBackground = ((layer.Flags & 0x08) == 0x08);
		// The type is TEXTURE::Style of JJ2+, the textured background of the sky layer is drawn in it, a reflection of any
		// other layer too, and any layer the MLLE stream gives a surface. JJ2+ draws one of an inner surface only where the
		// layer is, placed as an ordinary one - any style but a wave, which is always placed so.
		const bool isReflection = (hasTexturedBackground && layer.TexturedBackgroundType == TexturedStyleReflection);
		const bool isTextured = (hasTexturedBackground && (isSky || isReflection || layer.TextureSurface != 0));
		const bool isInnerSurface = (isTextured && layer.TextureSurface >= 3 && layer.TexturedBackgroundType != TexturedStyleWave);
		const bool hasTexturedSpeeds = (isTextured && !isInnerSurface);
		// A sky, a tunnel or a cylinder of SURFACE::FULLSCREEN is drawn in the perspective of JJ2+, and so is one of the sky
		// layer of SURFACE::LEGACY in a level of JJ2+, which JJ2+ draws exactly as FULLSCREEN (measured), and a cylinder always
		// (it has no other model, see Tiles::LayerDescription::HasPlusPerspective()). The sky layer of any other level, and
		// LEGACY of another layer, which JJ2+ draws with properties of layer 8, keep the look the sky layers always had here.
		const bool hasPlusStyle = (layer.TexturedBackgroundType == TexturedStyleWarpHorizon ||
			layer.TexturedBackgroundType == TexturedStyleTunnel || layer.TexturedBackgroundType == TexturedStyleCylinder);
		const bool hasPlusPerspective = (hasTexturedSpeeds && hasPlusStyle && (layer.TextureSurface == 2 ||
			layer.TexturedBackgroundType == TexturedStyleCylinder || (forPlusLevel && isSky && layer.TextureSurface <= 1)));
		if (hasTexturedSpeeds && hasPlusStyle && !hasPlusPerspective && !isSky && layer.TextureSurface == 1) {
			LOGW("Layer {} of level \"{}\" is textured as SURFACE::LEGACY, which JJ2+ draws with properties of layer 8, it's drawn as the sky layers always were here instead", index, LevelName);
		}
		record.OffsetX = layer.OffsetX;
		record.OffsetY = layer.OffsetY;

		// NORMAL of JJ2+ moves a layer by its auto speed, if it has one, or by its speed otherwise, which is the
		// default model without the speed; BOTHSPEEDS moves it by both and a textured layer has its auto speed
		// added to wherever the model places it. FITLEVEL ignores both.
		float speedX = layer.SpeedX;
		float speedY = layer.SpeedY;
		float autoSpeedX = layer.AutoSpeedX;
		float autoSpeedY = layer.AutoSpeedY;
		if (layer.SpeedModelX == LayerSectionSpeedModel::FitLevel ||
			(layer.SpeedModelX <= LayerSectionSpeedModel::Legacy && !hasTexturedSpeeds && std::abs(autoSpeedX) > 0.0f)) {
			speedX = 0.0f;
		}
		if (layer.SpeedModelY == LayerSectionSpeedModel::FitLevel ||
			(layer.SpeedModelY <= LayerSectionSpeedModel::Legacy && !hasTexturedSpeeds && std::abs(autoSpeedY) > 0.0f)) {
			speedY = 0.0f;
		}
		if (layer.SpeedModelX == LayerSectionSpeedModel::FitLevel) {
			autoSpeedX = 0.0f;
		}
		if (layer.SpeedModelY == LayerSectionSpeedModel::FitLevel) {
			autoSpeedY = 0.0f;
		}

		Tiles::LayerSpeedModel speedModelX = ConvertSpeedModel(layer.SpeedModelX);
		Tiles::LayerSpeedModel speedModelY = ConvertSpeedModel(layer.SpeedModelY);
		record.SpeedModels = (std::uint8_t)(((std::int32_t)speedModelX & 0x0f) | (((std::int32_t)speedModelY & 0x0f) << 4));
		record.SpeedX = speedX;
		record.SpeedY = speedY;
		// Per tick of the original, 70 a second, a layer here moves by its auto speed each of 60 frames a second
		record.AutoSpeedX = Tiles::LayerDescription::FromOriginalAutoSpeed(autoSpeedX, speedModelX);
		record.AutoSpeedY = Tiles::LayerDescription::FromOriginalAutoSpeed(autoSpeedY, speedModelY);
		record.Depth = (std::int16_t)layer.Depth;

		if (hasTexturedBackground && !isTextured) {
			LOGW("Layer {} of level \"{}\" has a textured background, only the sky layer can be drawn so without MLLE, it's drawn as tiles", index, LevelName);
		}
		if (!isTextured) {
			ConvertSpriteMode(layer.SpriteMode, layer.SpriteParam, record);
			return;
		}

		record.RendererType = layer.TexturedBackgroundType + (std::uint8_t)Tiles::LayerRendererType::Sky;
		// The fade color of a sky, a tunnel or a cylinder, the wavelengths and the speed of a wave, the tint opacity,
		// distance and distortion of a reflection
		record.Color[0] = layer.TexturedParams1;
		record.Color[1] = layer.TexturedParams2;
		record.Color[2] = layer.TexturedParams3;
		// ParallaxStarsEnabled, which JJ2+ takes as the blur of a reflection
		record.Color[3] = ((layer.Flags & 0x10) == 0x10 ? 255 : 0);
		if (layer.TexturedBackgroundType == TexturedStyleTunnel || layer.TexturedBackgroundType == TexturedStyleWave ||
			layer.TexturedBackgroundType == TexturedStyleCylinder || layer.TexturedBackgroundType == TexturedStyleReflection) {
			if (layer.TexturedBackgroundType == TexturedStyleTunnel) {
				// JJ2+ takes the stars as the spiral of a tunnel, which has no stars
				record.Textured.Flags = std::uint8_t(record.Color[3] != 0 ? Formats::TexturedLayerParams::Spiral : 0);
				record.Color[3] = 0;
			} else if (layer.TexturedBackgroundType == TexturedStyleWave) {
				// As the way the vertical waves of a wave move, which has no stars either
				record.Textured.Flags = std::uint8_t(record.Color[3] != 0 ? Formats::TexturedLayerParams::DistortionAngle : 0);
				record.Color[3] = 0;
			} else if (layer.TexturedBackgroundType == TexturedStyleCylinder) {
				// And as the half size of a cylinder, which has none either
				record.Textured.Flags = std::uint8_t(record.Color[3] != 0 ? Formats::TexturedLayerParams::HalfSize : 0);
				record.Color[3] = 0;
			}
			// A wave has no fade, the position of the fade is where JJ2+ keeps its amplitudes
			if (layer.TexturedBackgroundType != TexturedStyleWave) {
				record.Textured.Fade = layer.TextureFade;
			}
			record.Textured.FadeX = layer.TextureFadeX;
			record.Textured.FadeY = layer.TextureFadeY;
		} else {
			// The vanishing point and the horizon of a warp horizon and whether it fades, the defaults without the MLLE stream,
			// and the point a menu turns around, its rows of the palette, and whether they go from light to dark or a tile menu
			// is drawn at full size, are in the color
			if (layer.TexturedBackgroundType != TexturedStyleMenu && layer.TexturedBackgroundType != TexturedStyleTileMenu) {
				record.Textured.Fade = layer.TextureFade;
			}
			record.Textured.FadeX = layer.TextureFadeX;
			record.Textured.FadeY = layer.TextureFadeY;
		}
		if (isInnerSurface) {
			// SURFACE::INNERWINDOW or SURFACE::INNERLAYER, the speeds place the layer then and the inner speeds move its
			// texture inside it
			record.Textured.Flags = std::uint8_t(record.Textured.Flags | (layer.TextureSurface == 3
				? Formats::TexturedLayerParams::InnerWindow : Formats::TexturedLayerParams::InnerLayer));
			// All of them finite already, see ReadFiniteFloat(), the auto speeds per tick of the original as those of the layer
			record.Textured.InnerSpeedX = layer.InnerSpeedX;
			record.Textured.InnerSpeedY = layer.InnerSpeedY;
			record.Textured.InnerAutoSpeedX = layer.InnerAutoSpeedX * Tiles::LayerDescription::OriginalAutoSpeedScale;
			record.Textured.InnerAutoSpeedY = layer.InnerAutoSpeedY * Tiles::LayerDescription::OriginalAutoSpeedScale;
		} else if (hasPlusPerspective || (layer.TextureSurface == 2 && layer.TexturedBackgroundType != TexturedStyleWave)) {
			// SURFACE::FULLSCREEN (or LEGACY of the sky layer, see above), in the perspective of JJ2+ with the texture placed
			// as an ordinary layer, not as the sky layers have always been drawn here
			record.Textured.Flags = std::uint8_t(record.Textured.Flags | Formats::TexturedLayerParams::Fullscreen);
		}
		if (layer.TextureSource != 0) {
			// A texture of JJ2+ (TEXTURE::Texture) instead of the tiles of the layer, or an image of its own made by MLLE
			constexpr std::int32_t TextureBytes = Formats::TexturedLayerChunk::TextureSize * Formats::TexturedLayerChunk::TextureSize;
			texture.resize_for_overwrite(TextureBytes);
			if (layer.TextureSource < 0 && layer.TextureImage != nullptr) {
				std::memcpy(texture.data(), layer.TextureImage.get(), TextureBytes);
			} else if (layer.TextureSource < 0 || !textureLookup || !textureLookup(layer.TextureSource, texture.data())) {
				// The textures of JJ2+ come with it, not with the game, one that ripples a reflection alike is made up
				LOGW("Layer {} of level \"{}\" has texture {} of JJ2+, which isn't available, a texture is made up instead", index, LevelName, layer.TextureSource);
				MakeRippleTexture(texture.data());
			}
			if (hasPlusStyle && (hasPlusPerspective || isInnerSurface)) {
				// JJ2+ draws a sky, a tunnel or a cylinder from the upper half of the palette, index | 128 (measured), a wave
				// as it is - its own textures are there already, an image made by MLLE may not be
				for (std::uint8_t& index : texture) {
					index |= 0x80;
				}
			}
		}
	}

	Formats::TileRecord JJ2Level::ConvertLayerTile(std::uint16_t tileIdx) const
	{
		const std::uint16_t maxTiles = std::uint16_t(GetMaxSupportedTiles());
		const std::uint16_t lastTilesetTileIndex = std::uint16_t(maxTiles - _animCount);

		bool flipX = false, flipY = false;
		if ((tileIdx & 0x2000) != 0) {
			flipY = true;
			tileIdx -= 0x2000;
		}

		if ((tileIdx & ~(maxTiles | (maxTiles - 1))) != 0) {
			// Fix of bug in updated Psych2.j2l
			tileIdx = (std::uint16_t)((tileIdx & (maxTiles | (maxTiles - 1))) | maxTiles);
		}

		// Max. tiles is either 0x0400 or 0x1000 and doubles as a mask to separate flipped tiles.
		// In J2L, each flipped tile had a separate entry in the tile list, probably to make
		// the dictionary concept easier to handle.

		if ((tileIdx & maxTiles) > 0) {
			flipX = true;
			tileIdx -= maxTiles;
		}

		bool legacyTranslucent = false;
		bool invisible = false;
		bool heat = false;
		if (tileIdx < lastTilesetTileIndex) {
			legacyTranslucent = (_staticTiles[tileIdx].Type == 1);
			invisible = (_staticTiles[tileIdx].Type == 3);
			heat = (_staticTiles[tileIdx].Type == TileTypeHeatEffect);
		}

		std::uint8_t tileFlags = 0;
		if (flipX) {
			tileFlags |= Formats::TileRecord::FlipX;
		}
		if (flipY) {
			tileFlags |= Formats::TileRecord::FlipY;
		}

		if (legacyTranslucent) {
			tileFlags |= Formats::TileRecord::ModifierTranslucent;
		} else if (invisible) {
			tileFlags |= Formats::TileRecord::ModifierInvisible;
		} else if (heat) {
			tileFlags |= Formats::TileRecord::ModifierHeat;
		}

		Formats::TileRecord tile;
		tile.Flags = tileFlags;
		tile.TileId = tileIdx;
		return tile;
	}

	void JJ2Level::ConvertSpriteMode(std::uint8_t spriteMode, std::uint8_t spriteParam, Formats::LayerRecord& record)
	{
		// The modes the renderers can draw, any other is drawn as ordinary tiles. Translucent is as opaque as translucent
		// tiles are (see TileMap::MakeLayerTile()), an invisible layer is there and solid, but not drawn at all.
		record.RendererType = (std::uint8_t)Tiles::LayerRendererType::Default;
		record.Color[0] = 255;
		record.Color[1] = 255;
		record.Color[2] = 255;
		record.Color[3] = 255;

		switch (spriteMode) {
			case 1:		// SPRITE::TRANSLUCENT
				record.Color[3] = 192;
				break;
			case 2:		// SPRITE::TINTED
				record.RendererType = (std::uint8_t)Tiles::LayerRendererType::Tinted;
				record.Color[0] = spriteParam;
				record.Color[1] = 0;
				record.Color[2] = 0;
				break;
			case 4:		// SPRITE::INVISIBLE
				record.Color[3] = 0;
				break;
			case 5:		// SPRITE::SINGLECOLOR
			case 14:	// SPRITE::TRANSLUCENTCOLOR
				record.RendererType = (std::uint8_t)Tiles::LayerRendererType::SingleColor;
				record.Color[0] = spriteParam;
				record.Color[1] = 0;
				record.Color[2] = 0;
				record.Color[3] = (spriteMode == 14 ? 192 : 255);
				break;
			case 38:	// SPRITE::CHROMAKEY
				record.RendererType = (std::uint8_t)Tiles::LayerRendererType::ChromaKey;
				record.Color[0] = spriteParam;
				record.Color[1] = 0;
				record.Color[2] = 0;
				break;
			case 10:	// SPRITE::PALSHIFT
			case 16:	// SPRITE::TRANSLUCENTPALSHIFT
				record.RendererType = (std::uint8_t)Tiles::LayerRendererType::PaletteShift;
				record.Color[0] = spriteParam;
				record.Color[3] = (spriteMode == 16 ? 192 : 255);
				break;
		}
	}

	void JJ2Level::MakeRippleTexture(std::uint8_t* pixels)
	{
		// Value noise of four octaves, each of them on a lattice that fits the texture a whole number of times, so the
		// texture repeats without a seam, as the textures of JJ2+ do; always the same, the level is converted the same
		constexpr std::int32_t Size = Formats::TexturedLayerChunk::TextureSize;
		constexpr std::int32_t Octaves = 4;
		constexpr std::uint8_t FirstColor = 176;
		constexpr std::int32_t ColorCount = 32;

		auto lattice = [](std::int32_t x, std::int32_t y, std::int32_t octave) {
			std::uint32_t h = std::uint32_t(x) * 0x8DA6B343u ^ std::uint32_t(y) * 0xD8163841u ^ std::uint32_t(octave + 1) * 0xCB1AB31Fu;
			h ^= h >> 13;
			h *= 0x5BD1E995u;
			h ^= h >> 15;
			return float(h & 0xFFFFu) / 65535.0f;
		};

		SmallVector<float, 0> values;
		values.resize_for_overwrite(std::size_t(Size) * Size);
		float min = 1.0f, max = 0.0f;
		for (std::int32_t y = 0; y < Size; y++) {
			for (std::int32_t x = 0; x < Size; x++) {
				float value = 0.0f, weight = 1.0f;
				for (std::int32_t octave = 0; octave < Octaves; octave++) {
					const std::int32_t cells = (4 << octave);
					const float fx = float(x * cells) / Size;
					const float fy = float(y * cells) / Size;
					const std::int32_t x0 = std::int32_t(fx), y0 = std::int32_t(fy);
					const std::int32_t x1 = (x0 + 1) % cells, y1 = (y0 + 1) % cells;
					// Smoothly between the points of the lattice, the ripples have no edges
					float tx = fx - float(x0), ty = fy - float(y0);
					tx = tx * tx * (3.0f - 2.0f * tx);
					ty = ty * ty * (3.0f - 2.0f * ty);
					const float top = lattice(x0, y0, octave) + (lattice(x1, y0, octave) - lattice(x0, y0, octave)) * tx;
					const float bottom = lattice(x0, y1, octave) + (lattice(x1, y1, octave) - lattice(x0, y1, octave)) * tx;
					value += (top + (bottom - top) * ty) * weight;
					weight *= 0.5f;
				}
				values[std::size_t(y) * Size + x] = value;
				min = std::min(min, value);
				max = std::max(max, value);
			}
		}

		// Over the whole gradient, as the textures of JJ2+ are
		const float scale = (max > min ? float(ColorCount) / (max - min) : 0.0f);
		for (std::size_t i = 0; i < values.size(); i++) {
			pixels[i] = std::uint8_t(FirstColor + std::clamp<std::int32_t>(std::int32_t((values[i] - min) * scale), 0, ColorCount - 1));
		}
	}

	Formats::EventRecord JJ2Level::MakeEventRecord(const ConversionResult& converted, bool illuminate, std::uint8_t difficulty, std::uint8_t generatorFlags, std::int32_t generatorDelay)
	{
		Formats::EventRecord record;
		record.Type = std::uint16_t(converted.Type);

		// TODO: Flag 0x08 not used
		record.Flags = 0;
		if (illuminate) {
			record.Flags |= Formats::EventRecord::Illuminated;
		}
		if (difficulty != 2 /*Hard*/) {
			record.Flags |= Formats::EventRecord::Easy;
		}
		if (difficulty == 0 /*All*/) {
			record.Flags |= Formats::EventRecord::Normal;
		}
		if (difficulty != 1 /*Easy*/) {
			record.Flags |= Formats::EventRecord::Hard;
		}
		if (difficulty == 3 /*Multiplayer*/) {
			record.Flags |= Formats::EventRecord::MultiplayerOnly;
		}

		if (generatorDelay != -1) {
			record.Flags |= Formats::EventRecord::Generator;
			record.GeneratorFlags = generatorFlags;
			record.GeneratorDelay = std::uint8_t(generatorDelay);
		} else {
			record.GeneratorFlags = 0;
			record.GeneratorDelay = 0;
		}

		std::memcpy(record.Params, converted.Params, sizeof(record.Params));
		return record;
	}

	bool JJ2Level::StringHasSuffixIgnoreCase(StringView value, StringView suffix)
	{
		const std::size_t size = value.size();
		const std::size_t suffixSize = suffix.size();
		if (size < suffixSize) return false;

		for (std::size_t i = 0; i < suffixSize; i++) {
			if (tolower(value[size - suffixSize + i]) != suffix[i]) {
				return false;
			}
		}

		return true;
	}
}