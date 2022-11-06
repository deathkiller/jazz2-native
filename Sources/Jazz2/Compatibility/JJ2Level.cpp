#include "JJ2Level.h"
#include "EventConverter.h"
#include "../ContentResolver.h"

#include "../../nCine/Base/Algorithms.h"
#include "../../nCine/IO/CompressionUtils.h"
#include "../../nCine/IO/FileSystem.h"

namespace Jazz2::Compatibility
{
	void JJ2Level::Open(const StringView& path, bool strictParser)
	{
		auto s = fs::Open(path, FileAccessMode::Read);
		ASSERT_MSG(s->IsOpened(), "Cannot open file for reading");

		// Skip copyright notice
		s->Seek(180, SeekOrigin::Current);

		LevelName = fs::GetFileNameWithoutExtension(path);
		lowercaseInPlace(LevelName);

		JJ2Block headerBlock(s, 262 - 180);

		uint32_t magic = headerBlock.ReadUInt32();
		ASSERT_MSG(magic == 0x4C56454C /*LEVL*/, "Invalid magic string");

		uint32_t passwordHash = headerBlock.ReadUInt32();

		Name = headerBlock.ReadString(32, true);

		uint16_t version = headerBlock.ReadUInt16();
		_version = (version <= 514 ? JJ2Version::BaseGame : JJ2Version::TSF);

		_darknessColor = 0;
		_weatherType = WeatherType::None;
		_weatherIntensity = 0;

		int recordedSize = headerBlock.ReadInt32();
		ASSERT_MSG(!strictParser || s->GetSize() == recordedSize, "Unexpected file size");

		// Get the CRC; would check here if it matches if we knew what variant it is AND what it applies to
		// Test file across all CRC32 variants + Adler had no matches to the value obtained from the file
		// so either the variant is something else or the CRC is not applied to the whole file but on a part
		int recordedCRC = headerBlock.ReadInt32();

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
			LoadMlleData(mlleBlock, mlleVersion, strictParser);
		}
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
		int headerSize = block.ReadInt32();

		String secondLevelName = block.ReadString(32, true);
		ASSERT_MSG(!strictParser || Name == secondLevelName, "Level name mismatch");

		Tileset = block.ReadString(32, true);
		BonusLevel = block.ReadString(32, true);
		NextLevel = block.ReadString(32, true);
		SecretLevel = block.ReadString(32, true);
		Music = block.ReadString(32, true);

		for (int i = 0; i < TextEventStringsCount; ++i) {
			_textEventStrings[i] = block.ReadString(512, true);
		}

		LoadLayerMetadata(block, strictParser);

		uint16_t staticTilesCount = block.ReadUInt16();
		ASSERT_MSG(!strictParser || GetMaxSupportedTiles() - _animCount == staticTilesCount, "Tile count mismatch");

		LoadStaticTileData(block, strictParser);

		// The unused XMask field
		block.DiscardBytes(GetMaxSupportedTiles());

		LoadAnimatedTiles(block, strictParser);
	}

	void JJ2Level::LoadStaticTileData(JJ2Block& block, bool strictParser)
	{
		int tileCount = GetMaxSupportedTiles();
		_staticTiles = std::make_unique<TilePropertiesSection[]>(tileCount);

		for (int i = 0; i < tileCount; ++i) {
			int tileEvent = block.ReadInt32();

			auto& tile = _staticTiles[i];
			tile.Event.EventType = (JJ2Event)(uint8_t)(tileEvent & 0x000000FF);
			tile.Event.Difficulty = (uint8_t)((tileEvent & 0x0000C000) >> 14);
			tile.Event.Illuminate = ((tileEvent & 0x00002000) >> 13 == 1);
			tile.Event.TileParams = (uint32_t)(((tileEvent >> 12) & 0x000FFFF0) | ((tileEvent >> 8) & 0x0000000F));
		}
		for (int i = 0; i < tileCount; ++i) {
			_staticTiles[i].Flipped = block.ReadBool();
		}

		for (int i = 0; i < tileCount; ++i) {
			_staticTiles[i].Type = block.ReadByte();
		}
	}

	void JJ2Level::LoadAnimatedTiles(JJ2Block& block, bool strictParser)
	{
		_animatedTiles = std::make_unique<AnimatedTileSection[]>(_animCount);

		for (int i = 0; i < _animCount; i++) {
			auto& tile = _animatedTiles[i];
			tile.Delay = block.ReadUInt16();
			tile.DelayJitter = block.ReadUInt16();
			tile.ReverseDelay = block.ReadUInt16();
			tile.IsReverse = block.ReadBool();
			tile.Speed = block.ReadByte(); // 0-70
			tile.FrameCount = block.ReadByte();

			for (int j = 0; j < 64; j++) {
				tile.Frames[j] = block.ReadUInt16();
			}
		}
	}

	void JJ2Level::LoadLayerMetadata(JJ2Block& block, bool strictParser)
	{
		_layers = std::make_unique<LayerSection[]>(JJ2LayerCount);

		for (int i = 0; i < JJ2LayerCount; ++i) {
			_layers[i].Flags = block.ReadUInt32();
		}

		for (int i = 0; i < JJ2LayerCount; ++i) {
			_layers[i].Type = block.ReadByte();
		}

		for (int i = 0; i < JJ2LayerCount; ++i) {
			_layers[i].Used = block.ReadBool();
		}

		for (int i = 0; i < JJ2LayerCount; ++i) {
			_layers[i].Width = block.ReadInt32();
		}

		// This is related to how data is presented in the file; the above is a WYSIWYG version, solely shown on the UI
		for (int i = 0; i < JJ2LayerCount; ++i) {
			_layers[i].InternalWidth = block.ReadInt32();
		}

		for (int i = 0; i < JJ2LayerCount; ++i) {
			_layers[i].Height = block.ReadInt32();
		}

		for (int i = 0; i < JJ2LayerCount; ++i) {
			_layers[i].Depth = block.ReadInt32();
		}

		for (int i = 0; i < JJ2LayerCount; ++i) {
			_layers[i].DetailLevel = block.ReadByte();
		}

		for (int i = 0; i < JJ2LayerCount; ++i) {
			_layers[i].WaveX = block.ReadFloatEncoded();
		}

		for (int i = 0; i < JJ2LayerCount; ++i) {
			_layers[i].WaveY = block.ReadFloatEncoded();
		}

		for (int i = 0; i < JJ2LayerCount; ++i) {
			_layers[i].SpeedX = block.ReadFloatEncoded();
		}

		for (int i = 0; i < JJ2LayerCount; ++i) {
			_layers[i].SpeedY = block.ReadFloatEncoded();
		}

		for (int i = 0; i < JJ2LayerCount; ++i) {
			_layers[i].AutoSpeedX = block.ReadFloatEncoded();
		}

		for (int i = 0; i < JJ2LayerCount; ++i) {
			_layers[i].AutoSpeedY = block.ReadFloatEncoded();
		}

		for (int i = 0; i < JJ2LayerCount; ++i) {
			_layers[i].TexturedBackgroundType = block.ReadByte();
		}

		for (int i = 0; i < JJ2LayerCount; ++i) {
			_layers[i].TexturedParams1 = block.ReadByte();
			_layers[i].TexturedParams2 = block.ReadByte();
			_layers[i].TexturedParams3 = block.ReadByte();
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
		if (lastTileEvent.EventType == JJ2Event::MCE) {
			_hasPit = true;
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

	void JJ2Level::LoadMlleData(JJ2Block& block, uint32_t version, bool strictParser)
	{
		if (version != 0x104) {
			LOGW_X("Unsupported version of MLLE stream found in level \"%s\"", LevelName.data());
			return;
		}

		bool isSnowing = block.ReadBool();
		bool isSnowingOutdoorsOnly = block.ReadBool();
		uint8_t snowIntensity = block.ReadByte();
		uint8_t snowType = block.ReadByte();

		if (isSnowing) {
			_weatherIntensity = snowIntensity;
			_weatherType = (WeatherType)(snowType + 1);
			if (_weatherType != WeatherType::None && isSnowingOutdoorsOnly) {
				_weatherType |= WeatherType::OutdoorsOnly;
			}
		}

		bool warpsTransmuteCoins = block.ReadBool(); // TODO
		bool delayGeneratedCrateOrigins = block.ReadBool(); // TODO
		int32_t echo = block.ReadInt32(); // TODO
		_darknessColor = block.ReadUInt32();
		float waterChangeSpeed = block.ReadFloat(); // TODO
		uint8_t waterInteraction = block.ReadByte(); // TODO
		int32_t waterLayer = block.ReadInt32(); // TODO
		uint8_t waterLighting = block.ReadByte(); // TODO
		float waterLevel = block.ReadFloat(); // TODO
		uint32_t waterGradient1 = block.ReadUInt32(); // TODO
		uint32_t waterGradient2 = block.ReadUInt32(); // TODO

		// TODO
	}

	void JJ2Level::Convert(const String& targetPath, const EventConverter& eventConverter, const std::function<LevelToken(MutableStringView&)>& levelTokenConversion)
	{
		auto so = fs::Open(targetPath, FileAccessMode::Write);
		ASSERT_MSG(so->IsOpened(), "Cannot open file for writing");

		so->WriteValue<uint64_t>(0x2095A59FF0BFBBEF);
		so->WriteValue<uint8_t>(ContentResolver::LevelFile);

		// Flags
		uint16_t flags = 0;
		if (_hasPit) {
			flags |= 0x01;
		}
		if (_verticalMPSplitscreen) {
			flags |= 0x02;
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
		so->WriteValue<uint16_t>(flags);

		GrowableMemoryFile co(1024 * 1024);

		co.WriteValue<uint8_t>((uint8_t)Name.size());
		co.Write(Name.data(), Name.size());

		lowercaseInPlace(NextLevel);
		lowercaseInPlace(SecretLevel);
		lowercaseInPlace(BonusLevel);

		WriteLevelName(co, NextLevel, levelTokenConversion);
		WriteLevelName(co, SecretLevel, levelTokenConversion);
		WriteLevelName(co, BonusLevel, levelTokenConversion);

		// Default Tileset
		lowercaseInPlace(Tileset);
		if (StringHasSuffixIgnoreCase(Tileset, ".j2t"_s)) {
			Tileset = Tileset.exceptSuffix(4);
		}
		co.WriteValue<uint8_t>((uint8_t)Tileset.size());
		co.Write(Tileset.data(), Tileset.size());

		// Default Music
		lowercaseInPlace(Music);
		if (Music.findOr('.', Music.end()) == Music.end()) {
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

		// Text Event Strings
		co.WriteValue<uint8_t>(TextEventStringsCount);
		for (int i = 0; i < TextEventStringsCount; i++) {
			size_t textLength = _textEventStrings[i].size();
			for (int j = 0; j < textLength; j++) {
				if (_textEventStrings[i][j] == '@') {
					_textEventStrings[i][j] = '\n';
				}
			}

			co.WriteValue<uint16_t>((uint8_t)textLength);
			co.Write(_textEventStrings[i].data(), textLength);
		}

		// TODO: Additional Tilesets

		uint16_t lastTilesetTileIndex = (uint16_t)(maxTiles - _animCount);

		// Animated Tiles
		co.WriteValue<uint16_t>(_animCount);
		for (int i = 0; i < _animCount; i++) {
			auto& tile = _animatedTiles[i];
			co.WriteValue<uint8_t>(tile.FrameCount);
			co.WriteValue<uint16_t>((uint16_t)(tile.Speed == 0 ? 0 : 16 * 50 / tile.Speed));
			co.WriteValue<uint16_t>(tile.Delay);
			co.WriteValue<uint16_t>(tile.DelayJitter);
			co.WriteValue<uint8_t>(tile.IsReverse ? 1 : 0);
			co.WriteValue<uint16_t>(tile.ReverseDelay);

			for (int j = 0; j < tile.FrameCount; j++) {
				// Max. tiles is either 0x0400 or 0x1000 and doubles as a mask to separate flipped tiles.
				// In J2L, each flipped tile had a separate entry in the tile list, probably to make
				// the dictionary concept easier to handle.
				bool flipX = false, flipY = false;
				uint16_t tileIdx = tile.Frames[j];
				if ((tileIdx & maxTiles) > 0) {
					flipX = true;
					tileIdx -= maxTiles;
				}

				if (tileIdx >= lastTilesetTileIndex) {
					tileIdx = _animatedTiles[tileIdx - lastTilesetTileIndex].Frames[0];
				}

				uint8_t tileFlags = 0x00;
				if (flipX) {
					tileFlags |= 0x01; // Flip X
				}
				if (flipY) {
					tileFlags |= 0x02; // Flip Y
				}

				if (_staticTiles[tile.Frames[j]].Type == 1) {
					tileFlags |= 0x10; // Legacy Translucent
				} else if (_staticTiles[tile.Frames[j]].Type == 3) {
					tileFlags |= 0x20; // Invisible
				}

				co.WriteValue<uint8_t>(tileFlags);
				co.WriteValue<uint16_t>(tileIdx);
			}
		}

		// Layers
		int layerCount = 0;
		for (int i = 0; i < JJ2LayerCount; i++) {
			if (_layers[i].Used) {
				layerCount++;
			}
		}

		co.WriteValue<uint8_t>(layerCount);
		for (int i = 0; i < JJ2LayerCount; i++) {
			auto& layer = _layers[i];
			if (layer.Used) {
				bool isSky = (i == 7);
				bool isSprite = (i == 3);
				co.WriteValue<uint8_t>(isSprite ? 2 : (isSky ? 1 : 0));	// Layer type
				co.WriteValue<uint8_t>(layer.Flags & 0xff);				// Layer flags

				co.WriteValue<int32_t>(layer.Width);
				co.WriteValue<int32_t>(layer.Height);

				if (!isSprite) {
					bool hasTexturedBackground = ((layer.Flags & 0x08) == 0x08);
					if (isSky && !hasTexturedBackground) {
						co.WriteValue<float>(180.0f);
						co.WriteValue<float>(-300.0f);
					} else {
						co.WriteValue<float>(0.0f);
						co.WriteValue<float>(0.0f);
					}

					co.WriteValue<float>(layer.SpeedX);
					co.WriteValue<float>(layer.SpeedY);
					co.WriteValue<float>(layer.AutoSpeedX);
					co.WriteValue<float>(layer.AutoSpeedY);
					co.WriteValue<int16_t>((int16_t)layer.Depth);

					if (isSky && hasTexturedBackground) {
						co.WriteValue<uint8_t>(layer.TexturedBackgroundType + 1);
						co.WriteValue<uint8_t>(layer.TexturedParams1);
						co.WriteValue<uint8_t>(layer.TexturedParams2);
						co.WriteValue<uint8_t>(layer.TexturedParams3);
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

				int flags = 0;
				if (tileEvent.Illuminate) flags |= 0x04; // Illuminated
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

				// TODO: Flag 0x08 not used

				JJ2Event eventType;
				int generatorDelay;
				uint8_t generatorFlags;
				if (tileEvent.EventType == JJ2Event::MODIFIER_GENERATOR) {
					// Generators are converted differently
					uint8_t eventParams[8];
					EventConverter::ConvertParamInt(tileEvent.TileParams, {
						{ JJ2ParamUInt, 8 },	// Event
						{ JJ2ParamUInt, 8 },	// Delay
						{ JJ2ParamBool, 1 }		// Initial Delay
					}, eventParams);

					eventType = (JJ2Event)eventParams[0];
					generatorDelay = eventParams[1];
					generatorFlags = (uint8_t)eventParams[2];
				} else {
					eventType = tileEvent.EventType;
					generatorDelay = -1;
					generatorFlags = 0;
				}

				ConversionResult converted = eventConverter.TryConvert(this, eventType, tileEvent.TileParams);

				// If the event is unsupported or can't be converted, add it to warning list
				if (eventType != JJ2Event::EMPTY && converted.Type == EventType::Empty) {
					//int count;
					//_unsupportedEvents.TryGetValue(eventType, out count);
					//_unsupportedEvents[eventType] = (count + 1);
				}

				co.WriteValue<uint16_t>((uint16_t)converted.Type);

				bool allZeroes = true;
				if (converted.Type != EventType::Empty) {
					for (int i = 0; i < _countof(converted.Params); i++) {
						if (converted.Params[i] != 0) {
							allZeroes = false;
							break;
						}
					}
				}

				if (allZeroes) {
					if (generatorDelay == -1) {
						co.WriteValue<uint8_t>(flags | 0x01 /*NoParams*/);
					} else {
						co.WriteValue<uint8_t>(flags | 0x01 /*NoParams*/ | 0x02 /*Generator*/);
						co.WriteValue<uint8_t>(generatorFlags);
						co.WriteValue<uint8_t>(generatorDelay);
					}
				} else {
					if (generatorDelay == -1) {
						co.WriteValue<uint8_t>(flags);
					} else {
						co.WriteValue<uint8_t>(flags | 0x02 /*Generator*/);
						co.WriteValue<uint8_t>(generatorFlags);
						co.WriteValue<uint8_t>(generatorDelay);
					}

					co.Write(converted.Params, sizeof(converted.Params));
				}
			}
		}
		
		int32_t compressedSize = CompressionUtils::GetMaxDeflatedSize(co.GetSize());
		std::unique_ptr<uint8_t[]> compressedBuffer = std::make_unique<uint8_t[]>(compressedSize);
		compressedSize = CompressionUtils::Deflate(co.GetBuffer(), co.GetSize(), compressedBuffer.get(), compressedSize);
		ASSERT(compressedSize > 0);

		so->WriteValue<int32_t>(compressedSize);
		so->WriteValue<int32_t>(co.GetSize());
		so->Write(compressedBuffer.get(), compressedSize);
	}

	void JJ2Level::WriteLevelName(GrowableMemoryFile& so, MutableStringView value, const std::function<LevelToken(MutableStringView&)>& levelTokenConversion)
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

	bool JJ2Level::StringHasSuffixIgnoreCase(const StringView& value, const StringView& suffix)
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