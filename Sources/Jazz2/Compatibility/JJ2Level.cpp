#include "JJ2Level.h"
#include "JJ2Strings.h"
#include "EventConverter.h"
#include "../ContentResolver.h"
#include "../Tiles/TileMap.h"

#include "../../nCine/Base/Algorithms.h"

#include <Containers/StringUtils.h>
#include <IO/DeflateStream.h>
#include <IO/FileSystem.h>

using namespace Death::IO;

namespace Jazz2::Compatibility
{
	bool JJ2Level::Open(const StringView path, bool strictParser)
	{
		auto s = fs::Open(path, FileAccessMode::Read);
		RETURNF_ASSERT_MSG(s->IsValid(), "Cannot open file for reading");

		// Skip copyright notice
		s->Seek(180, SeekOrigin::Current);

		LevelName = fs::GetFileNameWithoutExtension(path);
		StringUtils::lowercaseInPlace(LevelName);

		JJ2Block headerBlock(s, 262 - 180);

		uint32_t magic = headerBlock.ReadUInt32();
		RETURNF_ASSERT_MSG(magic == 0x4C56454C /*LEVL*/, "Invalid magic string");

		// passwordHash is 3 bytes
		headerBlock.DiscardBytes(3);

		_isHidden = headerBlock.ReadBool();

		DisplayName = headerBlock.ReadString(32, true);

		uint16_t version = headerBlock.ReadUInt16();
		_version = (version <= 514 ? JJ2Version::BaseGame : JJ2Version::TSF);

		_darknessColor = 0;
		_weatherType = WeatherType::None;
		_weatherIntensity = 0;
		_waterLevel = 32767;

		int recordedSize = headerBlock.ReadInt32();
		RETURNF_ASSERT_MSG(!strictParser || s->GetSize() == recordedSize, "Unexpected file size");

		// Get the CRC; would check here if it matches if we knew what variant it is AND what it applies to
		// Test file across all CRC32 variants + Adler had no matches to the value obtained from the file
		// so either the variant is something else or the CRC is not applied to the whole file but on a part
		/*int recordedCRC =*/ headerBlock.ReadInt32();

		// Read the lengths, uncompress the blocks and bail if any block could not be uncompressed
		// This could look better without all the copy-paste, but meh.
		int infoBlockPackedSize = headerBlock.ReadInt32();
		int infoBlockUnpackedSize = headerBlock.ReadInt32();
		int eventBlockPackedSize = headerBlock.ReadInt32();
		int eventBlockUnpackedSize = headerBlock.ReadInt32();
		int dictBlockPackedSize = headerBlock.ReadInt32();
		int dictBlockUnpackedSize = headerBlock.ReadInt32();
		int layoutBlockPackedSize = headerBlock.ReadInt32();
		int layoutBlockUnpackedSize = headerBlock.ReadInt32();

		JJ2Block infoBlock(s, infoBlockPackedSize, infoBlockUnpackedSize);
		JJ2Block eventBlock(s, eventBlockPackedSize, eventBlockUnpackedSize);
		JJ2Block dictBlock(s, dictBlockPackedSize, dictBlockUnpackedSize);
		JJ2Block layoutBlock(s, layoutBlockPackedSize, layoutBlockUnpackedSize);

		LoadMetadata(infoBlock, strictParser);
		LoadEvents(eventBlock, strictParser);
		LoadLayers(dictBlock, dictBlockUnpackedSize / 8, layoutBlock, strictParser);

		// Try to read MLLE data stream
		uint32_t mlleMagic = s->ReadValue<uint32_t>();
		if (mlleMagic == 0x454C4C4D /*MLLE*/) {
			uint32_t mlleVersion = s->ReadValue<uint32_t>();
			int mlleBlockPackedSize = s->ReadValue<int>();
			int mlleBlockUnpackedSize = s->ReadValue<int>();

			JJ2Block mlleBlock(s, mlleBlockPackedSize, mlleBlockUnpackedSize);
			LoadMlleData(mlleBlock, mlleVersion, path, strictParser);
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

		_verticalMPSplitscreen = block.ReadBool();
		_isMpLevel = block.ReadBool();

		// This should be the same as size of block in the start?
		/*int headerSize =*/ block.ReadInt32();

		String secondLevelName = block.ReadString(32, true);
		//ASSERT_MSG(!strictParser || DisplayName == secondLevelName, "Level name mismatch");

		Tileset = block.ReadString(32, true);
		BonusLevel = block.ReadString(32, true);
		NextLevel = block.ReadString(32, true);
		SecretLevel = block.ReadString(32, true);
		Music = block.ReadString(32, true);

		for (int i = 0; i < TextEventStringsCount; ++i) {
			_textEventStrings[i] = block.ReadString(512, true);
		}

		LoadLayerMetadata(block, strictParser);

		/*uint16_t staticTilesCount =*/ block.ReadUInt16();
		//ASSERT_MSG(!strictParser || GetMaxSupportedTiles() - _animCount == staticTilesCount, "Tile count mismatch");

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
			tile.FrameCount = block.ReadByte();

			for (std::int32_t j = 0; j < 64; j++) {
				tile.Frames[j] = block.ReadUInt16();
			}
		}
	}

	void JJ2Level::LoadLayerMetadata(JJ2Block& block, bool strictParser)
	{
		_layers.resize(JJ2LayerCount);

		for (int i = 0; i < JJ2LayerCount; i++) {
			_layers[i].Flags = block.ReadUInt32();
		}

		for (int i = 0; i < JJ2LayerCount; i++) {
			_layers[i].Type = block.ReadByte();
		}

		for (int i = 0; i < JJ2LayerCount; i++) {
			_layers[i].Used = block.ReadBool();
			_layers[i].Visible = true;
		}

		for (int i = 0; i < JJ2LayerCount; i++) {
			_layers[i].Width = block.ReadInt32();
		}

		// This is related to how data is presented in the file, the above is a WYSIWYG version, solely shown on the UI
		for (int i = 0; i < JJ2LayerCount; i++) {
			_layers[i].InternalWidth = block.ReadInt32();
		}

		for (int i = 0; i < JJ2LayerCount; i++) {
			_layers[i].Height = block.ReadInt32();
		}

		for (int i = 0; i < JJ2LayerCount; i++) {
			_layers[i].Depth = block.ReadInt32();
		}

		for (int i = 0; i < JJ2LayerCount; i++) {
			_layers[i].DetailLevel = block.ReadByte();
		}

		for (int i = 0; i < JJ2LayerCount; i++) {
			_layers[i].OffsetX = block.ReadFloatEncoded();
		}

		for (int i = 0; i < JJ2LayerCount; i++) {
			_layers[i].OffsetY = block.ReadFloatEncoded();
		}

		for (int i = 0; i < JJ2LayerCount; i++) {
			_layers[i].SpeedX = block.ReadFloatEncoded();
		}

		for (int i = 0; i < JJ2LayerCount; i++) {
			_layers[i].SpeedY = block.ReadFloatEncoded();
		}

		for (int i = 0; i < JJ2LayerCount; i++) {
			_layers[i].AutoSpeedX = block.ReadFloatEncoded();
			_layers[i].SpeedModelX = LayerSectionSpeedModel::Normal;
		}

		for (int i = 0; i < JJ2LayerCount; i++) {
			_layers[i].AutoSpeedY = block.ReadFloatEncoded();
			_layers[i].SpeedModelY = LayerSectionSpeedModel::Normal;
		}

		for (int i = 0; i < JJ2LayerCount; i++) {
			_layers[i].TexturedBackgroundType = block.ReadByte();
		}

		for (int i = 0; i < JJ2LayerCount; i++) {
			_layers[i].TexturedParams1 = block.ReadByte();
			_layers[i].TexturedParams2 = block.ReadByte();
			_layers[i].TexturedParams3 = block.ReadByte();

			_layers[i].SpriteMode = 0;
			_layers[i].SpriteParam = 0;
		}
	}

	void JJ2Level::LoadEvents(JJ2Block& block, bool strictParser)
	{
		int width = _layers[3].Width;
		int height = _layers[3].Height;
		if (width <= 0 && height <= 0) {
			return;
		}

		_events = std::make_unique<TileEventSection[]>(width * height);

		for (int y = 0; y < _layers[3].Height; y++) {
			for (int x = 0; x < width; x++) {
				uint32_t eventData = block.ReadUInt32();

				auto& tileEvent = _events[x + y * width];
				tileEvent.EventType = (JJ2Event)(uint8_t)(eventData & 0x000000FF);
				tileEvent.Difficulty = (uint8_t)((eventData & 0x00000300) >> 8);
				tileEvent.Illuminate = ((eventData & 0x00000400) >> 10 == 1);
				tileEvent.TileParams = ((eventData & 0xFFFFF000) >> 12);
			}
		}

		auto& lastTileEvent = _events[(width * height) - 1];
		if (lastTileEvent.EventType == JJ2Event::MODIFIER_ONE_WAY) {
			_hasPit = false;
			_hasPitInstantDeath = false;
		} else if (lastTileEvent.EventType == JJ2Event::MCE) {
			_hasPit = true;
			_hasPitInstantDeath = true;
		} else {
			_hasPit = true;
			_hasPitInstantDeath = false;
		}

		for (int i = 0; i < width * height; i++) {
			if (_events[i].EventType == JJ2Event::CTF_BASE) {
				_hasCTF = true;
			} else if (_events[i].EventType == JJ2Event::WARP_ORIGIN) {
				if (((_events[i].TileParams >> 16) & 1) == 1) {
					_hasLaps = true;
				}
			}
		}
	}

	void JJ2Level::LoadLayers(JJ2Block& dictBlock, int dictLength, JJ2Block& layoutBlock, bool strictParser)
	{
		struct DictionaryEntry {
			uint16_t Tiles[4];
		};

		std::unique_ptr<DictionaryEntry[]> dictionary = std::make_unique<DictionaryEntry[]>(dictLength);
		for (int i = 0; i < dictLength; i++) {
			auto& entry = dictionary[i];
			for (int j = 0; j < 4; j++) {
				entry.Tiles[j] = dictBlock.ReadUInt16();
			}
		}

		for (int i = 0; i < JJ2LayerCount; ++i) {
			auto& layer = _layers[i];

			if (layer.Used) {
				layer.Tiles = std::make_unique<uint16_t[]>(layer.InternalWidth * layer.Height);

				for (int y = 0; y < layer.Height; y++) {
					for (int x = 0; x < layer.InternalWidth; x += 4) {
						uint16_t dictIdx = layoutBlock.ReadUInt16();

						for (int j = 0; j < 4; j++) {
							if (j + x >= layer.Width) {
								break;
							}

							layer.Tiles[j + x + y * layer.InternalWidth] = dictionary[dictIdx].Tiles[j];
						}
					}
				}
			} else {
				// Array will be initialized with zeros
				layer.Tiles = std::make_unique<uint16_t[]>(layer.Width * layer.Height);
			}
		}
	}

	void JJ2Level::LoadMlleData(JJ2Block& block, uint32_t version, const StringView& path, bool strictParser)
	{
		if (version > 0x106) {
			LOGW("MLLE stream version 0x%x in level \"%s\" is not supported", version, LevelName.data());
			return;
		}

		bool isSnowing = block.ReadBool();
		bool isSnowingOutdoorsOnly = block.ReadBool();
		uint8_t snowIntensity = block.ReadByte();
		// Weather particles type (Snow, Flower, Rain, Leaf)
		uint8_t snowType = block.ReadByte();

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
		int32_t echo = block.ReadInt32();
		uint32_t darknessColorBgra = block.ReadUInt32();
		_darknessColor = ((darknessColorBgra >> 16) & 0xff) | (darknessColorBgra & 0x0000ff00) | ((darknessColorBgra << 16) & 0x00ff0000);
		// TODO: Water level change speed
		float waterChangeSpeed = block.ReadFloat();
		// TODO: How player should react to being underwater (PositionBased, Swim, LowGravity)
		uint8_t waterInteraction = block.ReadByte();
		int32_t waterLayer = block.ReadInt32();
		// TODO: How water and ambient lighting should interact in the level (None, Global, Lagunicus)
		uint8_t waterLighting = block.ReadByte();
		_waterLevel = (uint16_t)block.ReadFloat();
		uint32_t waterGradientStart = block.ReadUInt32();
		uint32_t waterGradientStop = block.ReadUInt32();

		// Level palette
		_useLevelPalette = block.ReadBool();
		if (_useLevelPalette) {
			block.ReadRawBytes(_levelPalette, sizeof(_levelPalette));

			if (version >= 0x106) {
				// TODO: Reapply palette on death
				bool reapplyPaletteOnDeath = block.ReadBool();
			}
		}

		// TODO: Additional palettes
		if (version >= 0x106) {
			uint8_t extraPaletteCount = block.ReadByte();
			while (extraPaletteCount-- != 0) {
				int32_t nameLength = block.ReadUint7bitEncoded();
				block.DiscardBytes(nameLength);
				block.DiscardBytes(256 * 3);
			}
		}

		// TODO: Recolorable sprites
		int32_t recolorableSpriteListSize = (version >= 0x105 ? 20 : 11);
		for (int32_t i = 0; i < recolorableSpriteListSize; ++i) {
			// NOTE: Recolorable sprite list was expanded in MLLE-Include-1.5
			if (block.ReadBool()) {
				block.DiscardBytes(256);
			}
		}

		// Extra tilesets
		int32_t extraTilesetCount = block.ReadByte();
		for (int32_t i = 0; i < extraTilesetCount; i++) {
			auto& tileset = ExtraTilesets.emplace_back();

			int tilesetNameLength = block.ReadUint7bitEncoded();
			tileset.Name = block.ReadString(tilesetNameLength, false);

			tileset.Offset = block.ReadUInt16();
			tileset.Count = block.ReadUInt16();
			tileset.HasPaletteRemapping = block.ReadBool();
			if (tileset.HasPaletteRemapping) {
				block.ReadRawBytes(tileset.PaletteRemapping, sizeof(tileset.PaletteRemapping));
			}
		}

		// Additional layers
		if (version >= 0x102) {
			int32_t layerCount = block.ReadInt32();

			for (int32_t i = 8; i < layerCount; i += 8) {
				char numberBuffer[16];
				i32tos(i / 8, numberBuffer);

				StringView foundDot = path.findLastOr('.', path.end());
				String extraLayersPath = fs::FindPathCaseInsensitive(path.prefix(foundDot.begin()) + "-MLLE-Data-"_s + numberBuffer + ".j2l"_s);

				JJ2Level extraLayersFile;
				if (extraLayersFile.Open(extraLayersPath, strictParser)) {
					for (int j = 0; j < 8 && (i + j) < layerCount; j++) {
						_layers.emplace_back(std::move(extraLayersFile._layers[j]));
					}
				} else {
					for (int j = 0; j < 8 && (i + j) < layerCount; j++) {
						_layers.emplace_back(LayerSection{});
					}
				}
			}

			SmallVector<int32_t, JJ2LayerCount * 2> layerOrder(layerCount);
			int nextExtraLayerIdx = JJ2LayerCount;
			for (int i = 0; i < layerCount; i++) {
				int8_t id = (int8_t)block.ReadByte();
				int32_t idx;
				if (id >= 0) {
					idx = id;
				} else {
					idx = nextExtraLayerIdx++;
				}
				auto& layer = _layers[idx];
				layerOrder[idx] = i;

				int layerNameLength = block.ReadUint7bitEncoded();
				//String layerName = block.ReadString(layerNameLength, false);
				block.DiscardBytes(layerNameLength);

				layer.Visible = !block.ReadBool();
				layer.SpriteMode = block.ReadByte();
				layer.SpriteParam = block.ReadByte();
				int rotationAngle = block.ReadInt32();
				int rotationRadiusMult = block.ReadInt32();

				if (version >= 0x106) {
					layer.SpeedModelX = (LayerSectionSpeedModel)block.ReadByte();
					layer.SpeedModelY = (LayerSectionSpeedModel)block.ReadByte();
					// TODO: Texture surface effect (Untextured, Legacy, Fullscreen, InnerWindow, InnerLayer)
					//layer.TextureSurface = block.ReadByte();
					//layer.Fade = block.ReadByte();
					//layer.FadeX = block.ReadFloat();
					//layer.FadeY = block.ReadFloat();
					//layer.InnerSpeedX = block.ReadFloat();
					//layer.InnerSpeedY = block.ReadFloat();
					//layer.InnerAutoSpeedX = block.ReadFloat();
					//layer.InnerAutoSpeedY = block.ReadFloat();
					block.DiscardBytes(26);

					int8_t texture = (int8_t)block.ReadByte();
					if (texture < 0) {
						//layer.TextureImage = block.ReadBytes(256 * 256);
						block.DiscardBytes(256 * 256);

					}
				}
			}

			// Sprite layer has zero depth
			int zeroDepthIdx = layerOrder[3];

			// Adjust depth of all layers
			for (size_t i = 0; i < _layers.size(); i++) {
				int newIdx = layerOrder[i];

				auto& layer = _layers[i];
				layer.Depth = (newIdx - zeroDepthIdx) * 100;
				if (layer.Depth < -200) {
					layer.Depth = -200 - (200 - layer.Depth) / 20;
				} else if (layer.Depth > 300) {
					layer.Depth = 300 + (layer.Depth - 300) / 20;
				}
			}

			// TODO: Edited tiles were added in MLLE-Include-1.3
			// TODO: Weapons were added in MLLE-Include-1.5(w)
			// TODO: Off-grid objects were added in MLLE-Include-1.6
		}
	}

	void JJ2Level::Convert(const StringView targetPath, const EventConverter& eventConverter, const std::function<LevelToken(const StringView)>& levelTokenConversion)
	{
		auto so = fs::Open(targetPath, FileAccessMode::Write);
		ASSERT_MSG(so->IsValid(), "Cannot open file for writing");

		so->WriteValue<uint64_t>(0x2095A59FF0BFBBEF);
		so->WriteValue<uint8_t>(ContentResolver::LevelFile);

		// Preprocess events first, so they can change some level properties
		for (int y = 0; y < _layers[3].Height; y++) {
			for (int x = 0; x < _layers[3].Width; x++) {
				auto& tileEvent = _events[x + y * _layers[3].Width];

				JJ2Event eventType;
				if (tileEvent.EventType == JJ2Event::MODIFIER_GENERATOR) {
					// Generators are converted differently
					uint8_t eventParams[8];
					EventConverter::ConvertParamInt(tileEvent.TileParams, {
						{ JJ2ParamUInt, 8 },	// Event
						{ JJ2ParamUInt, 8 },	// Delay
						{ JJ2ParamBool, 1 }		// Initial Delay
					}, eventParams);

					eventType = (JJ2Event)eventParams[0];
					tileEvent.GeneratorDelay = eventParams[1];
					tileEvent.GeneratorFlags = (uint8_t)eventParams[2];
				} else {
					eventType = tileEvent.EventType;
					tileEvent.GeneratorDelay = -1;
					tileEvent.GeneratorFlags = 0;
				}

				tileEvent.Converted = eventConverter.TryConvert(this, eventType, tileEvent.TileParams);
			}
		}

		// Flags
		uint16_t flags = 0;
		if (_hasPit) {
			flags |= 0x01;
		}
		if (_hasPitInstantDeath) {
			flags |= 0x02;
		}
		if (_useLevelPalette) {
			flags |= 0x04;
		}
		if (_isHidden) {
			flags |= 0x08;
		}
		if (_isMpLevel) {
			flags |= 0x10;
			if (_hasLaps) {
				flags |= 0x20;
			}
			if (_hasCTF) {
				flags |= 0x40;
			}
		}
		if (_verticalMPSplitscreen) {
			flags |= 0x80;
		}
		so->WriteValue<uint16_t>(flags);

		MemoryStream ms(1024 * 1024);
		{
			DeflateWriter co(ms);

			String formattedName = JJ2Strings::RecodeString(DisplayName, true);
			co.WriteValue<uint8_t>((uint8_t)formattedName.size());
			co.Write(formattedName.data(), formattedName.size());

			StringUtils::lowercaseInPlace(NextLevel);
			StringUtils::lowercaseInPlace(SecretLevel);
			StringUtils::lowercaseInPlace(BonusLevel);

			WriteLevelName(co, NextLevel, levelTokenConversion);
			WriteLevelName(co, SecretLevel, levelTokenConversion);
			WriteLevelName(co, BonusLevel, levelTokenConversion);

			// Default Tileset
			StringUtils::lowercaseInPlace(Tileset);
			if (StringHasSuffixIgnoreCase(Tileset, ".j2t"_s)) {
				Tileset = Tileset.exceptSuffix(4);
			}
			co.WriteValue<uint8_t>((uint8_t)Tileset.size());
			co.Write(Tileset.data(), Tileset.size());

			// Default Music
			StringUtils::lowercaseInPlace(Music);
			if (Music.find('.') == nullptr) {
				String music = Music + ".j2b"_s;
				co.WriteValue<uint8_t>((uint8_t)music.size());
				co.Write(music.data(), music.size());
			} else {
				co.WriteValue<uint8_t>((uint8_t)Music.size());
				co.Write(Music.data(), Music.size());
			}

			co.WriteValue<uint8_t>(_darknessColor & 0xff);
			co.WriteValue<uint8_t>((_darknessColor >> 8) & 0xff);
			co.WriteValue<uint8_t>((_darknessColor >> 16) & 0xff);
			co.WriteValue<uint8_t>((uint8_t)std::min(LightingStart * 255 / 64, 255));

			co.WriteValue<uint8_t>((uint8_t)_weatherType);
			co.WriteValue<uint8_t>(_weatherIntensity);
			co.WriteValue<uint16_t>(_waterLevel);

			// Find caption tile
			uint16_t maxTiles = (uint16_t)GetMaxSupportedTiles();

			uint16_t captionTileId = 0;
			for (uint16_t i = 0; i < maxTiles; i++) {
				if (_staticTiles[i].Type == 4) {
					captionTileId = i;
					break;
				}
			}
			co.WriteValue<uint16_t>(captionTileId);

			// Custom palette
			if (_useLevelPalette) {
				for (int i = 0; i < sizeof(_levelPalette); i += 3) {
					// Expand JJ2+ RGB palette to RGBA
					uint32_t color = (uint32_t)_levelPalette[i] | ((uint32_t)_levelPalette[i + 1] << 8) | ((uint32_t)_levelPalette[i + 2] << 16) | 0xff000000;
					co.WriteValue<uint32_t>(color);
				}
			}

			// Extra Tilesets
			co.WriteValue<uint8_t>((uint8_t)ExtraTilesets.size());
			for (auto& tileset : ExtraTilesets) {
				uint8_t tilesetFlags = 0;
				if (tileset.HasPaletteRemapping) {
					tilesetFlags |= 0x01;
				}
				co.WriteValue<uint8_t>(tilesetFlags);

				StringUtils::lowercaseInPlace(tileset.Name);
				if (StringHasSuffixIgnoreCase(tileset.Name, ".j2t"_s)) {
					tileset.Name = tileset.Name.exceptSuffix(4);
				}

				co.WriteValue<uint8_t>((uint8_t)tileset.Name.size());
				co.Write(tileset.Name.data(), tileset.Name.size());

				co.WriteValue<uint16_t>(tileset.Offset);
				co.WriteValue<uint16_t>(tileset.Count);

				if (tileset.HasPaletteRemapping) {
					co.Write(tileset.PaletteRemapping, sizeof(tileset.PaletteRemapping));
				}
			}

			// Text Event Strings
			co.WriteValue<uint8_t>(TextEventStringsCount);
			for (int i = 0; i < TextEventStringsCount; i++) {
				String& text = _textEventStrings[i];

				bool isLevelToken = false;
				for (uint8_t textId : _levelTokenTextIds) {
					if (i == textId) {
						isLevelToken = true;
						break;
					}
				}

				if (isLevelToken) {
					String adjustedText = ""_s;
					auto levelTokens = text.split('|');
					for (int j = 0; j < levelTokens.size(); j++) {
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

					co.WriteValue<std::uint16_t>((std::uint16_t)adjustedText.size());
					co.Write(adjustedText.data(), adjustedText.size());
				} else {
					String formattedText = JJ2Strings::RecodeString(text);
					co.WriteValue<std::uint16_t>((std::uint16_t)formattedText.size());
					co.Write(formattedText.data(), formattedText.size());
				}
			}

			std::uint16_t lastTilesetTileIndex = (std::uint16_t)(maxTiles - _animCount);

			// Animated Tiles
			co.WriteValue<uint16_t>(_animCount);
			for (std::int32_t i = 0; i < _animCount; i++) {
				auto& tile = _animatedTiles[i];
				co.WriteValue<std::uint8_t>(tile.FrameCount);
				co.WriteValue<std::uint16_t>((std::uint16_t)(tile.Speed == 0 ? 0 : 16 * 50 / tile.Speed));
				co.WriteValue<std::uint16_t>(tile.Delay);
				co.WriteValue<std::uint16_t>(tile.DelayJitter);
				co.WriteValue<std::uint8_t>(tile.IsPingPong ? 1 : 0);
				co.WriteValue<std::uint16_t>(tile.ReverseDelay);

				for (std::int32_t j = 0; j < tile.FrameCount; j++) {
					// Max. tiles is either 0x0400 or 0x1000 and doubles as a mask to separate flipped tiles.
					// In J2L, each flipped tile had a separate entry in the tile list, probably to make
					// the dictionary concept easier to handle.
					bool flipX = false, flipY = false;
					uint16_t tileIdx = tile.Frames[j];
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
							LOGE("Animated tile references undefined tile %u in level \"%s\" (max. tile count is %u, anim. count is %u)", tileIdx, LevelName.data(), maxTiles, _animCount);
							tileIdx = 0;
						}
					}

					std::uint8_t tileFlags = 0x00;
					if (flipX) {
						tileFlags |= 0x01; // Flip X
					}
					if (flipY) {
						tileFlags |= 0x02; // Flip Y
					}

					if (_staticTiles[tileIdx].Type == 1) {
						tileFlags |= 0x10; // Legacy Translucent
					} else if (_staticTiles[tileIdx].Type == 3) {
						tileFlags |= 0x20; // Invisible
					}

					co.WriteValue<std::uint8_t>(tileFlags);
					co.WriteValue<std::uint16_t>(tileIdx);
				}
			}

			// Layers
			std::int32_t layerCount = 0;
			for (std::int32_t i = 0; i < _layers.size(); i++) {
				auto& layer = _layers[i];
				if (layer.Width == 0 || layer.Height == 0) {
					layer.Used = false;
				}
				if (layer.Used) {
					layerCount++;
				}
			}

			co.WriteValue<uint8_t>(layerCount);
			for (std::int32_t i = 0; i < _layers.size(); i++) {
				auto& layer = _layers[i];
				if (layer.Used) {
					bool isSky = (i == 7);
					bool isSprite = (i == 3);
					co.WriteValue<uint8_t>(isSprite ? 2 : (isSky ? 1 : 0));	// Layer type

					uint16_t flags = (uint16_t)(layer.Flags & (0x01 | 0x02 | 0x04)); // RepeatX, RepeatY, UseInherentOffset are mapped 1:1
					if (layer.Visible) {
						flags |= 0x08;
					}
					co.WriteValue<uint16_t>(flags);	// Layer flags

					co.WriteValue<int32_t>(layer.Width);
					co.WriteValue<int32_t>(layer.Height);

					if (!isSprite) {
						Tiles::LayerSpeedModel speedModelX, speedModelY;
						switch (layer.SpeedModelX) {
							case LayerSectionSpeedModel::Legacy: speedModelX = Tiles::LayerSpeedModel::AlwaysOnTop; break;
							case LayerSectionSpeedModel::FitLevel: speedModelX = Tiles::LayerSpeedModel::FitLevel; break;
							case LayerSectionSpeedModel::SpeedMultipliers: speedModelX = Tiles::LayerSpeedModel::SpeedMultipliers; break;
							default: speedModelX = Tiles::LayerSpeedModel::Default; break;
						}
						switch (layer.SpeedModelY) {
							case LayerSectionSpeedModel::Legacy: speedModelY = Tiles::LayerSpeedModel::AlwaysOnTop; break;
							case LayerSectionSpeedModel::FitLevel: speedModelY = Tiles::LayerSpeedModel::FitLevel; break;
							case LayerSectionSpeedModel::SpeedMultipliers: speedModelY = Tiles::LayerSpeedModel::SpeedMultipliers; break;
							default: speedModelY = Tiles::LayerSpeedModel::Default; break;
						}
						uint8_t combinedSpeedModel = ((int)speedModelX & 0x0f) | (((int)speedModelY & 0x0f) << 4);
						co.WriteValue<uint8_t>(combinedSpeedModel);

						bool hasTexturedBackground = ((layer.Flags & 0x08) == 0x08);
						if (isSky && !hasTexturedBackground && layer.SpeedModelX <= LayerSectionSpeedModel::Legacy && layer.SpeedModelY <= LayerSectionSpeedModel::Legacy) {
							co.WriteValue<float>(180.0f);
							co.WriteValue<float>(-300.0f);
						} else {
							co.WriteValue<float>(layer.OffsetX);
							co.WriteValue<float>(layer.OffsetY);
						}

						float speedX = layer.SpeedX;
						float speedY = layer.SpeedY;
						float autoSpeedX = layer.AutoSpeedX;
						float autoSpeedY = layer.AutoSpeedY;
						if (layer.SpeedModelX == LayerSectionSpeedModel::FitLevel ||
							(layer.SpeedModelX <= LayerSectionSpeedModel::Legacy && !hasTexturedBackground && std::abs(autoSpeedX) > 0.0f)) {
							speedX = 0.0f;
						}
						if (layer.SpeedModelY == LayerSectionSpeedModel::FitLevel ||
							(layer.SpeedModelY <= LayerSectionSpeedModel::Legacy && !hasTexturedBackground && std::abs(autoSpeedY) > 0.0f)) {
							speedY = 0.0f;
						}
						if (layer.SpeedModelX == LayerSectionSpeedModel::FitLevel) {
							autoSpeedX = 0.0f;
						}
						if (layer.SpeedModelY == LayerSectionSpeedModel::FitLevel) {
							autoSpeedY = 0.0f;
						}

						co.WriteValue<float>(speedX);
						co.WriteValue<float>(speedY);
						co.WriteValue<float>(autoSpeedX);
						co.WriteValue<float>(autoSpeedY);
						co.WriteValue<int16_t>((int16_t)layer.Depth);

						if (isSky && hasTexturedBackground) {
							co.WriteValue<uint8_t>(layer.TexturedBackgroundType + (uint8_t)Tiles::LayerRendererType::Sky);
							co.WriteValue<uint8_t>(layer.TexturedParams1);
							co.WriteValue<uint8_t>(layer.TexturedParams2);
							co.WriteValue<uint8_t>(layer.TexturedParams3);
							co.WriteValue<uint8_t>((layer.Flags & 0x10) == 0x10 ? 255 : 0);	// ParallaxStarsEnabled
						} else if (layer.SpriteMode == 2) {
							co.WriteValue<uint8_t>((uint8_t)Tiles::LayerRendererType::Tinted);
							co.WriteValue<uint8_t>(layer.SpriteParam);
							co.WriteValue<uint8_t>(0);
							co.WriteValue<uint8_t>(0);
							co.WriteValue<uint8_t>(255);
						} else {
							co.WriteValue<uint8_t>((uint8_t)Tiles::LayerRendererType::Default);
							co.WriteValue<uint8_t>(255);
							co.WriteValue<uint8_t>(255);
							co.WriteValue<uint8_t>(255);
							co.WriteValue<uint8_t>(255);
						}
					}

					for (int y = 0; y < layer.Height; y++) {
						for (int x = 0; x < layer.Width; x++) {
							uint16_t tileIdx = layer.Tiles[y * layer.InternalWidth + x];

							bool flipX = false, flipY = false;
							if ((tileIdx & 0x2000) != 0) {
								flipY = true;
								tileIdx -= 0x2000;
							}

							if ((tileIdx & ~(maxTiles | (maxTiles - 1))) != 0) {
								// Fix of bug in updated Psych2.j2l
								tileIdx = (uint16_t)((tileIdx & (maxTiles | (maxTiles - 1))) | maxTiles);
							}

							// Max. tiles is either 0x0400 or 0x1000 and doubles as a mask to separate flipped tiles.
							// In J2L, each flipped tile had a separate entry in the tile list, probably to make
							// the dictionary concept easier to handle.

							if ((tileIdx & maxTiles) > 0) {
								flipX = true;
								tileIdx -= maxTiles;
							}

							bool animated = false;
							if (tileIdx >= lastTilesetTileIndex) {
								animated = true;
								tileIdx -= lastTilesetTileIndex;
							}

							bool legacyTranslucent = false;
							bool invisible = false;
							if (!animated && tileIdx < lastTilesetTileIndex) {
								legacyTranslucent = (_staticTiles[tileIdx].Type == 1);
								invisible = (_staticTiles[tileIdx].Type == 3);
							}

							uint8_t tileFlags = 0;
							if (flipX) {
								tileFlags |= 0x01;
							}
							if (flipY) {
								tileFlags |= 0x02;
							}
							if (animated) {
								tileFlags |= 0x04;
							}

							if (legacyTranslucent) {
								tileFlags |= 0x10;
							} else if (invisible) {
								tileFlags |= 0x20;
							}

							co.WriteValue<uint8_t>(tileFlags);
							co.WriteValue<uint16_t>(tileIdx);
						}
					}
				}
			}

			// Events
			for (int y = 0; y < _layers[3].Height; y++) {
				for (int x = 0; x < _layers[3].Width; x++) {
					auto& tileEvent = _events[x + y * _layers[3].Width];

					// TODO: Flag 0x08 not used
					int flags = 0;
					if (tileEvent.Illuminate) {
						flags |= 0x04; // Illuminated
					}
					if (tileEvent.Difficulty != 2 /*Hard*/) {
						flags |= 0x10; // Difficulty: Easy
					}
					if (tileEvent.Difficulty == 0 /*All*/) {
						flags |= 0x20; // Difficulty: Normal
					}
					if (tileEvent.Difficulty != 1 /*Easy*/) {
						flags |= 0x40; // Difficulty: Hard
					}
					if (tileEvent.Difficulty == 3 /*Multiplayer*/) {
						flags |= 0x80; // Multiplayer Only
					}

					co.WriteValue<uint16_t>((uint16_t)tileEvent.Converted.Type);

					bool allZeroes = true;
					if (tileEvent.Converted.Type != EventType::Empty) {
						for (int i = 0; i < countof(tileEvent.Converted.Params); i++) {
							if (tileEvent.Converted.Params[i] != 0) {
								allZeroes = false;
								break;
							}
						}
					}

					if (allZeroes) {
						if (tileEvent.GeneratorDelay == -1) {
							co.WriteValue<uint8_t>(flags | 0x01 /*NoParams*/);
						} else {
							co.WriteValue<uint8_t>(flags | 0x01 /*NoParams*/ | 0x02 /*Generator*/);
							co.WriteValue<uint8_t>(tileEvent.GeneratorFlags);
							co.WriteValue<uint8_t>(tileEvent.GeneratorDelay);
						}
					} else {
						if (tileEvent.GeneratorDelay == -1) {
							co.WriteValue<uint8_t>(flags);
						} else {
							co.WriteValue<uint8_t>(flags | 0x02 /*Generator*/);
							co.WriteValue<uint8_t>(tileEvent.GeneratorFlags);
							co.WriteValue<uint8_t>(tileEvent.GeneratorDelay);
						}

						co.Write(tileEvent.Converted.Params, sizeof(tileEvent.Converted.Params));
					}
				}
			}
		}

		so->WriteValue<std::int32_t>(ms.GetSize());
		so->Write(ms.GetBuffer(), ms.GetSize());

#if defined(DEATH_DEBUG)
		/*auto episodeName = fs::GetFileName(fs::GetDirectoryName(targetPath));
		if (episodeName != "unknown"_s) {
			int lastStringIdx = -1;
			for (int i = TextEventStringsCount - 1; i >= 0; i--) {
				String& text = _textEventStrings[i];
				if (!text.empty()) {
					lastStringIdx = i;
					break;
				}
			}

			if (lastStringIdx >= 0) {
				auto so = fs::Open(targetPath + ".h"_s, FileAccessMode::Write);
				ASSERT_MSG(so->IsValid(), "Cannot open file for writing");

				for (int i = 0; i <= lastStringIdx; i++) {
					String& text = _textEventStrings[i];

					bool isLevelToken = false;
					for (uint8_t textId : _levelTokenTextIds) {
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

	void JJ2Level::AddLevelTokenTextID(uint8_t textId)
	{
		_levelTokenTextIds.push_back(textId);
	}

	void JJ2Level::WriteLevelName(Stream& so, MutableStringView value, const std::function<LevelToken(MutableStringView&)>& levelTokenConversion)
	{
		if (!value.empty()) {
			MutableStringView adjustedValue = value;
			if (StringHasSuffixIgnoreCase(adjustedValue, ".j2l"_s) ||
				StringHasSuffixIgnoreCase(adjustedValue, ".lev"_s)) {
				adjustedValue = adjustedValue.exceptSuffix(4);
			}

			if (levelTokenConversion != nullptr) {
				LevelToken token = levelTokenConversion(adjustedValue);
				if (!token.Episode.empty()) {
					String fullName = token.Episode + "/"_s + token.Level;
					so.WriteValue<uint8_t>(fullName.size());
					so.Write(fullName.data(), fullName.size());
				} else {
					so.WriteValue<uint8_t>(token.Level.size());
					so.Write(token.Level.data(), token.Level.size());
				}
			} else {
				so.WriteValue<uint8_t>(adjustedValue.size());
				so.Write(adjustedValue.data(), adjustedValue.size());
			}
		} else {
			so.WriteValue<uint8_t>(0);
		}
	}

	bool JJ2Level::StringHasSuffixIgnoreCase(const StringView value, const StringView suffix)
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