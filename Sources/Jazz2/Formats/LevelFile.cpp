#include "LevelFile.h"

#include <algorithm>
#include <cstring>

using namespace Death::Containers;
using namespace Death::IO::Compression;

namespace Jazz2::Formats
{
	bool LevelFile::ReadHeader(Stream& s, LevelHeader& header)
	{
		const std::uint64_t signature = s.ReadValueAsLE<std::uint64_t>();
		const std::uint8_t fileType = s.ReadValue<std::uint8_t>();
		const std::uint16_t version = s.ReadValueAsLE<std::uint16_t>();
		if (signature != Signature || fileType != ContentFileType::Level || version != CurrentVersion) {
			return false;
		}
		header.Version = version;
		header.Flags = (LevelFlags)s.ReadValueAsLE<std::uint16_t>();
		header.BodySize = s.ReadValueAsLE<std::int32_t>();
		return (header.BodySize >= 0);
	}

	bool EventRecord::HasParams() const
	{
		for (std::int32_t i = 0; i < LevelFile::EventParamsSize; i++) {
			if (Params[i] != 0) {
				return true;
			}
		}
		return false;
	}

	LevelReader::LevelReader(Stream& s, const LevelHeader& header)
		: _header(header), _input(s, header.BodySize), _bufferPos(0), _bufferLength(0), _failed(false)
	{
	}

	bool LevelReader::Refill()
	{
		if (_failed) {
			return false;
		}

		// Whatever is left in the buffer is kept at its start
		std::int32_t left = _bufferLength - _bufferPos;
		if (left > 0 && _bufferPos > 0) {
			std::memmove(_buffer, _buffer + _bufferPos, left);
		}
		_bufferPos = 0;
		_bufferLength = left;

		std::int64_t bytesRead = _input.Read(_buffer + _bufferLength, BufferSize - _bufferLength);
		if (bytesRead <= 0) {
			return false;
		}
		_bufferLength += std::int32_t(bytesRead);
		return true;
	}

	bool LevelReader::ReadBytes(void* destination, std::int32_t size)
	{
		std::uint8_t* typed = static_cast<std::uint8_t*>(destination);
		while (size > 0) {
			if (_bufferPos >= _bufferLength && !Refill()) {
				// A truncated body reads as zeroes, the caller rejects it by IsValid()
				std::memset(typed, 0, size);
				_failed = true;
				return false;
			}
			std::int32_t chunk = std::min(size, _bufferLength - _bufferPos);
			std::memcpy(typed, _buffer + _bufferPos, chunk);
			_bufferPos += chunk;
			typed += chunk;
			size -= chunk;
		}
		return true;
	}

	bool LevelReader::SkipBytes(std::int64_t size)
	{
		while (size > 0) {
			if (_bufferPos >= _bufferLength && !Refill()) {
				_failed = true;
				return false;
			}
			std::int32_t chunk = std::int32_t(std::min<std::int64_t>(size, _bufferLength - _bufferPos));
			_bufferPos += chunk;
			size -= chunk;
		}
		return true;
	}

	std::uint8_t LevelReader::ReadUint8()
	{
		if (_bufferPos < _bufferLength) {
			return _buffer[_bufferPos++];
		}
		std::uint8_t value;
		ReadBytes(&value, sizeof(value));
		return value;
	}

	std::uint16_t LevelReader::ReadUint16()
	{
		std::uint8_t bytes[2];
		ReadBytes(bytes, sizeof(bytes));
		return std::uint16_t(bytes[0] | (bytes[1] << 8));
	}

	std::int16_t LevelReader::ReadInt16()
	{
		return std::int16_t(ReadUint16());
	}

	std::uint32_t LevelReader::ReadUint32()
	{
		std::uint8_t bytes[4];
		ReadBytes(bytes, sizeof(bytes));
		return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) | (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
	}

	std::int32_t LevelReader::ReadInt32()
	{
		return std::int32_t(ReadUint32());
	}

	float LevelReader::ReadFloat()
	{
		std::uint32_t bits = ReadUint32();
		float value;
		std::memcpy(&value, &bits, sizeof(value));
		return value;
	}

	std::uint32_t LevelReader::ReadVariableUint32()
	{
		std::uint32_t result = 0;
		std::uint32_t shift = 0;
		while (true) {
			std::uint8_t byte = ReadUint8();
			if (_failed) {
				break;
			}
			if DEATH_LIKELY(shift < 32) {
				result |= std::uint32_t(byte & 0x7F) << shift;
				shift += 7;
			}
			if ((byte & 0x80) == 0) {
				break;
			}
		}
		return result;
	}

	String LevelReader::ReadString8()
	{
		std::uint8_t length = ReadUint8();
		String value{NoInit, length};
		ReadBytes(value.data(), length);
		return value;
	}

	String LevelReader::ReadString16()
	{
		std::uint16_t length = ReadUint16();
		String value{NoInit, length};
		ReadBytes(value.data(), length);
		return value;
	}

	void LevelReader::ReadProperties(LevelProperties& properties)
	{
		properties.DisplayName = ReadString8();
		properties.Author = ReadString8();
		properties.NextLevel = ReadString8();
		properties.SecretLevel = ReadString8();
		properties.BonusLevel = ReadString8();
		properties.DefaultTileSet = ReadString8();
		properties.Music = ReadString8();
		properties.AmbientColor = ReadUint32();
		properties.Weather = ReadUint8();
		properties.WeatherIntensity = ReadUint8();
		properties.WaterLevel = ReadUint16();
		properties.CaptionTileId = ReadUint16();
	}

	bool LevelReader::ReadPalettes(std::uint32_t* levelPalette, SmallVectorImpl<PaletteRecord>* additionalPalettes)
	{
		bool hasLevelPalette = ((_header.Flags & LevelFlags::UseLevelPalette) == LevelFlags::UseLevelPalette);
		if (hasLevelPalette) {
			if (levelPalette != nullptr) {
				for (std::int32_t i = 0; i < LevelFile::ColorsPerPalette; i++) {
					levelPalette[i] = ReadUint32();
				}
			} else {
				SkipBytes(LevelFile::ColorsPerPalette * sizeof(std::uint32_t));
			}
		}

		std::uint8_t count = ReadUint8();
		for (std::int32_t i = 0; i < count; i++) {
			if (additionalPalettes != nullptr) {
				PaletteRecord& palette = additionalPalettes->emplace_back();
				palette.Name = ReadString8();
				for (std::int32_t j = 0; j < LevelFile::ColorsPerPalette; j++) {
					palette.Colors[j] = ReadUint32();
				}
			} else {
				SkipBytes(ReadUint8());
				SkipBytes(LevelFile::ColorsPerPalette * sizeof(std::uint32_t));
			}
		}

		return (hasLevelPalette && levelPalette != nullptr);
	}

	void LevelReader::ReadExtraTileSets(SmallVectorImpl<ExtraTileSetRecord>& tileSets)
	{
		std::uint8_t count = ReadUint8();
		for (std::int32_t i = 0; i < count; i++) {
			ExtraTileSetRecord& tileSet = tileSets.emplace_back();
			tileSet.Flags = ReadUint8();
			tileSet.Name = ReadString8();
			tileSet.Offset = ReadUint16();
			tileSet.Count = ReadUint16();
			std::memset(tileSet.Remapping, 0, sizeof(tileSet.Remapping));
			tileSet.AlternatePalette = 0;

			if ((tileSet.Flags & ExtraTileSetRecord::Remapped) != 0) {
				if ((tileSet.Flags & ExtraTileSetRecord::TrueColor) != 0) {
					tileSet.AlternatePalette = ReadUint8();
				} else {
					ReadBytes(tileSet.Remapping, sizeof(tileSet.Remapping));
				}
			}
			if ((tileSet.Flags & ExtraTileSetRecord::Embedded) != 0) {
				std::uint32_t size = ReadVariableUint32();
				// A tile set of all the tiles a level can have is far smaller, more is a damaged file
				if (size > 512 * 1024 * 1024) {
					_failed = true;
					return;
				}
				tileSet.Data.resize_for_overwrite(size);
				ReadBytes(tileSet.Data.data(), size);
			}
		}
	}

	void LevelReader::ReadTileDiffuseOverrides(SmallVectorImpl<TileDiffuseOverride>* overrides)
	{
		std::uint32_t count = ReadVariableUint32();
		for (std::uint32_t i = 0; i < count && !_failed; i++) {
			if (overrides != nullptr) {
				TileDiffuseOverride& tile = overrides->emplace_back();
				tile.TileId = ReadUint16();
				ReadBytes(tile.Indices, sizeof(tile.Indices));
			} else {
				SkipBytes(sizeof(std::uint16_t) + sizeof(TileDiffuseOverride::Indices));
			}
		}
	}

	void LevelReader::ReadTileMaskOverrides(SmallVectorImpl<TileMaskOverride>* overrides)
	{
		std::uint32_t count = ReadVariableUint32();
		for (std::uint32_t i = 0; i < count && !_failed; i++) {
			if (overrides != nullptr) {
				TileMaskOverride& tile = overrides->emplace_back();
				tile.TileId = ReadUint16();
				ReadBytes(tile.Mask, sizeof(tile.Mask));
			} else {
				SkipBytes(sizeof(std::uint16_t) + sizeof(TileMaskOverride::Mask));
			}
		}
	}

	void LevelReader::ReadTexts(SmallVectorImpl<String>& texts)
	{
		std::uint8_t count = ReadUint8();
		texts.reserve(texts.size() + count);
		for (std::int32_t i = 0; i < count; i++) {
			texts.push_back(ReadString16());
		}
	}

	void LevelReader::ReadAnimatedTiles(std::uint16_t& animatedTilesOffset, SmallVectorImpl<AnimatedTileRecord>& animatedTiles)
	{
		animatedTilesOffset = ReadUint16();
		std::uint16_t count = ReadUint16();
		animatedTiles.reserve(animatedTiles.size() + count);
		for (std::int32_t i = 0; i < count && !_failed; i++) {
			AnimatedTileRecord& animatedTile = animatedTiles.emplace_back();
			std::uint8_t frameCount = ReadUint8();
			animatedTile.FrameDuration16 = ReadUint16();
			animatedTile.Delay = ReadUint16();
			animatedTile.DelayJitter = ReadUint16();
			animatedTile.IsPingPong = ReadUint8();
			animatedTile.PingPongDelay = ReadUint16();
			animatedTile.Frames.reserve(frameCount);
			for (std::int32_t j = 0; j < frameCount; j++) {
				TileRecord& frame = animatedTile.Frames.emplace_back();
				frame.Flags = ReadUint8();
				frame.TileId = ReadUint16();
			}
		}
	}

	std::int32_t LevelReader::ReadLayerCount()
	{
		return ReadUint8();
	}

	void LevelReader::ReadLayerHeader(LayerRecord& layer)
	{
		layer.Kind = (LayerKind)ReadUint8();
		layer.Flags = ReadUint16();
		layer.Width = ReadInt32();
		layer.Height = ReadInt32();

		if (layer.Kind != LayerKind::Sprite) {
			layer.SpeedModels = ReadUint8();
			layer.OffsetX = ReadFloat();
			layer.OffsetY = ReadFloat();
			layer.SpeedX = ReadFloat();
			layer.SpeedY = ReadFloat();
			layer.AutoSpeedX = ReadFloat();
			layer.AutoSpeedY = ReadFloat();
			layer.Depth = ReadInt16();
			layer.RendererType = ReadUint8();
			ReadBytes(layer.Color, sizeof(layer.Color));
		} else {
			// The sprite layer is always drawn with its defaults, they aren't stored
			layer.SpeedModels = 0;
			layer.OffsetX = 0.0f;
			layer.OffsetY = 0.0f;
			layer.SpeedX = 1.0f;
			layer.SpeedY = 1.0f;
			layer.AutoSpeedX = 0.0f;
			layer.AutoSpeedY = 0.0f;
			layer.Depth = 50;
			layer.RendererType = 0;
			std::memset(layer.Color, 255, sizeof(layer.Color));
		}

		if (layer.Width < 0 || layer.Height < 0) {
			layer.Width = 0;
			layer.Height = 0;
			_failed = true;
		}
	}

	void LevelReader::ReadLayerTiles(const LayerRecord& layer, Function<void(std::int32_t, ArrayView<const TileRecord>)>&& callback)
	{
		constexpr std::int32_t TileRecordSize = 3;
		constexpr std::int32_t TilesPerBlock = 512;

		const std::int64_t tileCount = std::int64_t(layer.Width) * layer.Height;
		if (!callback) {
			SkipBytes(tileCount * TileRecordSize);
			return;
		}

		TileRecord block[TilesPerBlock];
		for (std::int64_t i = 0; i < tileCount; i += TilesPerBlock) {
			const std::int32_t count = std::int32_t(std::min<std::int64_t>(TilesPerBlock, tileCount - i));
			for (std::int32_t j = 0; j < count; j++) {
				if (_bufferLength - _bufferPos < TileRecordSize && !Refill() && _bufferLength - _bufferPos < TileRecordSize) {
					// A truncated stream reads as empty tiles; the caller rejects it by IsValid()
					_failed = true;
					std::memset(&block[j], 0, (count - j) * sizeof(TileRecord));
					break;
				}
				const std::uint8_t* record = &_buffer[_bufferPos];
				block[j].Flags = record[0];
				block[j].TileId = std::uint16_t(record[1] | (record[2] << 8));
				_bufferPos += TileRecordSize;
			}
			callback(std::int32_t(i), ArrayView<const TileRecord>(block, count));
		}
	}

	void LevelReader::ReadEvent(EventRecord& event)
	{
		event.Type = ReadUint16();
		event.Flags = ReadUint8();
		if ((event.Flags & EventRecord::Generator) != 0) {
			event.GeneratorFlags = ReadUint8();
			event.GeneratorDelay = ReadUint8();
		} else {
			event.GeneratorFlags = 0;
			event.GeneratorDelay = 0;
		}
		if ((event.Flags & EventRecord::NoParams) == 0) {
			ReadBytes(event.Params, sizeof(event.Params));
		} else {
			std::memset(event.Params, 0, sizeof(event.Params));
		}
	}

	void LevelReader::ReadEvents(std::int32_t width, std::int32_t height, Function<void(std::int32_t, std::int32_t, const EventRecord&)>&& callback)
	{
		EventRecord event;
		for (std::int32_t y = 0; y < height; y++) {
			for (std::int32_t x = 0; x < width; x++) {
				ReadEvent(event);
				if (callback) {
					callback(x, y, event);
				}
			}
		}
	}

	bool LevelReader::SkipToChunks()
	{
		ReadPalettes(nullptr, nullptr);
		SmallVector<ExtraTileSetRecord, 0> extraTileSets;
		ReadExtraTileSets(extraTileSets);
		ReadTileDiffuseOverrides(nullptr);
		ReadTileMaskOverrides(nullptr);
		SmallVector<String, 0> texts;
		ReadTexts(texts);
		std::uint16_t animatedTilesOffset;
		SmallVector<AnimatedTileRecord, 0> animatedTiles;
		ReadAnimatedTiles(animatedTilesOffset, animatedTiles);

		// The events are as large as the sprite layer
		std::int32_t eventsWidth = 0, eventsHeight = 0;
		std::int32_t layerCount = ReadLayerCount();
		for (std::int32_t i = 0; i < layerCount && !_failed; i++) {
			LayerRecord layer;
			ReadLayerHeader(layer);
			if (_failed) {
				break;
			}
			if (layer.Kind == LayerKind::Sprite) {
				eventsWidth = layer.Width;
				eventsHeight = layer.Height;
			}
			ReadLayerTiles(layer, nullptr);
		}
		ReadEvents(eventsWidth, eventsHeight, nullptr);
		SmallVector<OffGridEventRecord, 0> offGridEvents;
		ReadOffGridEvents(offGridEvents);
		return !_failed;
	}

	void LevelReader::ReadOffGridEvents(SmallVectorImpl<OffGridEventRecord>& events)
	{
		std::uint32_t count = ReadVariableUint32();
		for (std::uint32_t i = 0; i < count && !_failed; i++) {
			OffGridEventRecord& event = events.emplace_back();
			event.X = ReadVariableUint32();
			event.Y = ReadVariableUint32();
			ReadEvent(event.Event);
		}
	}

	void LevelReader::ReadChunks(SmallVectorImpl<ChunkRecord>& chunks, bool includeEditorOnly)
	{
		if (_failed) {
			return;
		}

		// The chunks are optional, a body that ends here has none
		if (_bufferPos >= _bufferLength && !Refill()) {
			return;
		}

		std::uint32_t count = ReadVariableUint32();
		for (std::uint32_t i = 0; i < count && !_failed; i++) {
			std::uint32_t tag = ReadUint32();
			std::uint32_t size = ReadVariableUint32();
			if (!includeEditorOnly && LevelFile::IsEditorOnlyChunk(tag)) {
				SkipBytes(size);
				continue;
			}

			ChunkRecord& chunk = chunks.emplace_back();
			chunk.Tag = tag;
			chunk.Data.resize_for_overwrite(size);
			ReadBytes(chunk.Data.data(), std::int32_t(size));
		}
	}

	LevelWriter::LevelWriter(Stream& target, LevelFlags flags, std::int32_t compressionLevel)
		: _target(&target), _flags(flags), _body(1024 * 1024), _layerTilesLeft(0), _eventsLeft(-1), _finished(false)
	{
		_target->WriteValueAsLE<std::uint64_t>(LevelFile::Signature);
		_target->WriteValue<std::uint8_t>(ContentFileType::Level);
		_target->WriteValueAsLE<std::uint16_t>(LevelFile::CurrentVersion);
		_target->WriteValueAsLE<std::uint16_t>(std::uint16_t(flags));

		_deflate = std::make_unique<DeflateWriter>(_body, compressionLevel);
	}

	void LevelWriter::WriteString8(StringView value)
	{
		std::size_t length = std::min(value.size(), std::size_t(UINT8_MAX));
		_deflate->WriteValue<std::uint8_t>(std::uint8_t(length));
		_deflate->Write(value.data(), std::int64_t(length));
	}

	void LevelWriter::WriteProperties(const LevelProperties& properties)
	{
		WriteString8(properties.DisplayName);
		WriteString8(properties.Author);
		WriteString8(properties.NextLevel);
		WriteString8(properties.SecretLevel);
		WriteString8(properties.BonusLevel);
		WriteString8(properties.DefaultTileSet);
		WriteString8(properties.Music);
		_deflate->WriteValueAsLE<std::uint32_t>(properties.AmbientColor);
		_deflate->WriteValue<std::uint8_t>(properties.Weather);
		_deflate->WriteValue<std::uint8_t>(properties.WeatherIntensity);
		_deflate->WriteValueAsLE<std::uint16_t>(properties.WaterLevel);
		_deflate->WriteValueAsLE<std::uint16_t>(properties.CaptionTileId);
	}

	void LevelWriter::WritePalettes(const std::uint32_t* levelPalette, ArrayView<const PaletteRecord> additionalPalettes)
	{
		if ((_flags & LevelFlags::UseLevelPalette) == LevelFlags::UseLevelPalette) {
			DEATH_ASSERT(levelPalette != nullptr, "The level palette is required by the level flags", );
			for (std::int32_t i = 0; i < LevelFile::ColorsPerPalette; i++) {
				_deflate->WriteValueAsLE<std::uint32_t>(levelPalette[i]);
			}
		}

		std::size_t count = std::min(additionalPalettes.size(), std::size_t(UINT8_MAX));
		_deflate->WriteValue<std::uint8_t>(std::uint8_t(count));
		for (std::size_t i = 0; i < count; i++) {
			const PaletteRecord& palette = additionalPalettes[i];
			WriteString8(palette.Name);
			for (std::int32_t j = 0; j < LevelFile::ColorsPerPalette; j++) {
				_deflate->WriteValueAsLE<std::uint32_t>(palette.Colors[j]);
			}
		}
	}

	void LevelWriter::WriteExtraTileSets(ArrayView<const ExtraTileSetRecord> tileSets)
	{
		std::size_t count = std::min(tileSets.size(), std::size_t(UINT8_MAX));
		_deflate->WriteValue<std::uint8_t>(std::uint8_t(count));
		for (std::size_t i = 0; i < count; i++) {
			const ExtraTileSetRecord& tileSet = tileSets[i];
			_deflate->WriteValue<std::uint8_t>(tileSet.Flags);
			WriteString8(tileSet.Name);
			_deflate->WriteValueAsLE<std::uint16_t>(tileSet.Offset);
			_deflate->WriteValueAsLE<std::uint16_t>(tileSet.Count);

			if ((tileSet.Flags & ExtraTileSetRecord::Remapped) != 0) {
				if ((tileSet.Flags & ExtraTileSetRecord::TrueColor) != 0) {
					_deflate->WriteValue<std::uint8_t>(tileSet.AlternatePalette);
				} else {
					_deflate->Write(tileSet.Remapping, sizeof(tileSet.Remapping));
				}
			}
			if ((tileSet.Flags & ExtraTileSetRecord::Embedded) != 0) {
				_deflate->WriteVariableUint32(std::uint32_t(tileSet.Data.size()));
				_deflate->Write(tileSet.Data.data(), std::int64_t(tileSet.Data.size()));
			}
		}
	}

	void LevelWriter::WriteTileDiffuseOverrides(ArrayView<const TileDiffuseOverride> overrides)
	{
		_deflate->WriteVariableUint32(std::uint32_t(overrides.size()));
		for (const TileDiffuseOverride& tile : overrides) {
			_deflate->WriteValueAsLE<std::uint16_t>(tile.TileId);
			_deflate->Write(tile.Indices, sizeof(tile.Indices));
		}
	}

	void LevelWriter::WriteTileMaskOverrides(ArrayView<const TileMaskOverride> overrides)
	{
		_deflate->WriteVariableUint32(std::uint32_t(overrides.size()));
		for (const TileMaskOverride& tile : overrides) {
			_deflate->WriteValueAsLE<std::uint16_t>(tile.TileId);
			_deflate->Write(tile.Mask, sizeof(tile.Mask));
		}
	}

	void LevelWriter::WriteTexts(ArrayView<const String> texts)
	{
		std::size_t count = std::min(texts.size(), std::size_t(UINT8_MAX));
		_deflate->WriteValue<std::uint8_t>(std::uint8_t(count));
		for (std::size_t i = 0; i < count; i++) {
			const String& text = texts[i];
			std::size_t length = std::min(text.size(), std::size_t(UINT16_MAX));
			_deflate->WriteValueAsLE<std::uint16_t>(std::uint16_t(length));
			_deflate->Write(text.data(), std::int64_t(length));
		}
	}

	void LevelWriter::WriteAnimatedTiles(std::uint16_t animatedTilesOffset, ArrayView<const AnimatedTileRecord> animatedTiles)
	{
		std::size_t count = std::min(animatedTiles.size(), std::size_t(UINT16_MAX));
		_deflate->WriteValueAsLE<std::uint16_t>(animatedTilesOffset);
		_deflate->WriteValueAsLE<std::uint16_t>(std::uint16_t(count));
		for (std::size_t i = 0; i < count; i++) {
			const AnimatedTileRecord& animatedTile = animatedTiles[i];
			std::size_t frameCount = std::min(animatedTile.Frames.size(), std::size_t(UINT8_MAX));
			_deflate->WriteValue<std::uint8_t>(std::uint8_t(frameCount));
			_deflate->WriteValueAsLE<std::uint16_t>(animatedTile.FrameDuration16);
			_deflate->WriteValueAsLE<std::uint16_t>(animatedTile.Delay);
			_deflate->WriteValueAsLE<std::uint16_t>(animatedTile.DelayJitter);
			_deflate->WriteValue<std::uint8_t>(animatedTile.IsPingPong);
			_deflate->WriteValueAsLE<std::uint16_t>(animatedTile.PingPongDelay);
			for (std::size_t j = 0; j < frameCount; j++) {
				_deflate->WriteValue<std::uint8_t>(animatedTile.Frames[j].Flags);
				_deflate->WriteValueAsLE<std::uint16_t>(animatedTile.Frames[j].TileId);
			}
		}
	}

	void LevelWriter::WriteLayerCount(std::int32_t count)
	{
		DEATH_DEBUG_ASSERT(count >= 0 && count <= UINT8_MAX);
		_deflate->WriteValue<std::uint8_t>(std::uint8_t(count));
	}

	void LevelWriter::WriteLayerHeader(const LayerRecord& layer)
	{
		DEATH_DEBUG_ASSERT(_layerTilesLeft == 0, "Tiles of the previous layer are missing");

		_deflate->WriteValue<std::uint8_t>(std::uint8_t(layer.Kind));
		_deflate->WriteValueAsLE<std::uint16_t>(layer.Flags);
		_deflate->WriteValueAsLE<std::int32_t>(layer.Width);
		_deflate->WriteValueAsLE<std::int32_t>(layer.Height);

		if (layer.Kind != LayerKind::Sprite) {
			_deflate->WriteValue<std::uint8_t>(layer.SpeedModels);
			_deflate->WriteValueAsLE<float>(layer.OffsetX);
			_deflate->WriteValueAsLE<float>(layer.OffsetY);
			_deflate->WriteValueAsLE<float>(layer.SpeedX);
			_deflate->WriteValueAsLE<float>(layer.SpeedY);
			_deflate->WriteValueAsLE<float>(layer.AutoSpeedX);
			_deflate->WriteValueAsLE<float>(layer.AutoSpeedY);
			_deflate->WriteValueAsLE<std::int16_t>(layer.Depth);
			_deflate->WriteValue<std::uint8_t>(layer.RendererType);
			_deflate->Write(layer.Color, sizeof(layer.Color));
		} else {
			// The event grid has the size of the sprite layer
			_eventsLeft = std::int64_t(layer.Width) * layer.Height;
		}

		_layerTilesLeft = std::int64_t(layer.Width) * layer.Height;
	}

	void LevelWriter::WriteLayerTiles(ArrayView<const TileRecord> tiles)
	{
		DEATH_DEBUG_ASSERT(std::int64_t(tiles.size()) <= _layerTilesLeft, "Too many tiles for the layer");

		// Packed into 3-byte records a block at a time, a write per value would be a call into the compressor each
		std::uint8_t block[512 * 3];
		std::size_t i = 0;
		while (i < tiles.size()) {
			std::size_t count = std::min(tiles.size() - i, std::size_t(512));
			for (std::size_t j = 0; j < count; j++) {
				const TileRecord& tile = tiles[i + j];
				block[j * 3] = tile.Flags;
				block[j * 3 + 1] = std::uint8_t(tile.TileId & 0xFF);
				block[j * 3 + 2] = std::uint8_t(tile.TileId >> 8);
			}
			_deflate->Write(block, std::int64_t(count * 3));
			i += count;
		}

		_layerTilesLeft -= std::int64_t(tiles.size());
	}

	void LevelWriter::WriteEventFields(const EventRecord& event)
	{
		bool hasParams = (event.Type != 0 && event.HasParams());

		std::uint8_t flags = std::uint8_t(event.Flags & ~EventRecord::NoParams);
		if (!hasParams) {
			flags |= EventRecord::NoParams;
		}

		_deflate->WriteValueAsLE<std::uint16_t>(event.Type);
		_deflate->WriteValue<std::uint8_t>(flags);
		if ((flags & EventRecord::Generator) != 0) {
			_deflate->WriteValue<std::uint8_t>(event.GeneratorFlags);
			_deflate->WriteValue<std::uint8_t>(event.GeneratorDelay);
		}
		if (hasParams) {
			_deflate->Write(event.Params, sizeof(event.Params));
		}
	}

	void LevelWriter::WriteEvent(const EventRecord& event)
	{
		DEATH_DEBUG_ASSERT(_layerTilesLeft == 0, "Tiles of the last layer are missing");
		DEATH_DEBUG_ASSERT(_eventsLeft > 0, "Too many events for the sprite layer");

		WriteEventFields(event);
		_eventsLeft--;
	}

	void LevelWriter::WriteOffGridEvents(ArrayView<const OffGridEventRecord> events)
	{
		DEATH_DEBUG_ASSERT(_eventsLeft == 0, "Events of the grid are missing");

		_deflate->WriteVariableUint32(std::uint32_t(events.size()));
		for (const OffGridEventRecord& event : events) {
			_deflate->WriteVariableUint32(event.X);
			_deflate->WriteVariableUint32(event.Y);
			WriteEventFields(event.Event);
		}
	}

	void LevelWriter::WriteChunks(ArrayView<const ChunkRecord> chunks, bool stripEditorOnly)
	{
		std::uint32_t count = 0;
		for (const ChunkRecord& chunk : chunks) {
			if (!stripEditorOnly || !LevelFile::IsEditorOnlyChunk(chunk.Tag)) {
				count++;
			}
		}
		if (count == 0) {
			// The list is optional, a level without chunks doesn't store even the count
			return;
		}

		_deflate->WriteVariableUint32(count);
		for (const ChunkRecord& chunk : chunks) {
			if (!stripEditorOnly || !LevelFile::IsEditorOnlyChunk(chunk.Tag)) {
				_deflate->WriteValueAsLE<std::uint32_t>(chunk.Tag);
				_deflate->WriteVariableUint32(std::uint32_t(chunk.Data.size()));
				_deflate->Write(chunk.Data.data(), std::int64_t(chunk.Data.size()));
			}
		}
	}

	bool LevelWriter::Finish()
	{
		if (_finished) {
			return false;
		}
		_finished = true;

		bool deflateValid = _deflate->IsValid();
		// Completes the compressed stream
		_deflate = nullptr;

		_target->WriteValueAsLE<std::int32_t>(std::int32_t(_body.GetSize()));
		_target->Write(_body.GetBuffer(), _body.GetSize());
		return (deflateValid && _target->IsValid());
	}
}
