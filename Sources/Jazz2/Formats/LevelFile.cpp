#include "LevelFile.h"
#include "VarUint.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>

#include <Containers/StringUtils.h>
#include <Utf8.h>

using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::IO::Compression;

namespace Jazz2::Formats
{
	namespace
	{
		// More is a damaged or crafted file, each of them costs a record in memory however small it's in the file
		constexpr std::uint32_t MaxChunks = 65535;
		// Size of an entry of TexturedLayerChunk without the inner speeds and the texture, the only one before them, what
		// follows in an entry of a newer one is skipped
		constexpr std::uint32_t TexturedLayerEntrySize = 2 * sizeof(float) + 2;
		// Size of an entry with the inner speeds and the kind of the texture, which an image of its own follows
		constexpr std::uint32_t TexturedLayerTextureEntrySize = TexturedLayerEntrySize + 4 * sizeof(float) + 1;
		// The kinds of the texture of an entry, the tiles of the layer or an image of its own
		constexpr std::uint8_t TexturedLayerTextureTiles = 0;
		constexpr std::uint8_t TexturedLayerTextureImage = 1;
		constexpr std::uint32_t TexturedLayerTextureBytes = std::uint32_t(TexturedLayerChunk::TextureSize * TexturedLayerChunk::TextureSize);

		float ReadFloatLE(const std::uint8_t* p)
		{
			std::uint32_t bits = std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
			float value;
			std::memcpy(&value, &bits, sizeof(value));
			return value;
		}

		void WriteFloatLE(SmallVectorImpl<std::uint8_t>& data, float value)
		{
			std::uint32_t bits;
			std::memcpy(&bits, &value, sizeof(bits));
			for (std::int32_t i = 0; i < 4; i++) {
				data.push_back(std::uint8_t(bits >> (i * 8)));
			}
		}

		// Replaces a value that isn't finite by its default, returns true if it did
		bool SanitizeFloat(float& value, float defaultValue)
		{
			if (std::isfinite(value)) {
				return false;
			}
			value = defaultValue;
			return true;
		}

		// Replaces the offsets and the speeds of a layer that aren't finite by their defaults, returns true if any was
		bool SanitizeLayerFloats(LayerRecord& layer)
		{
			bool changed = SanitizeFloat(layer.OffsetX, 0.0f);
			changed |= SanitizeFloat(layer.OffsetY, 0.0f);
			changed |= SanitizeFloat(layer.SpeedX, 1.0f);
			changed |= SanitizeFloat(layer.SpeedY, 1.0f);
			changed |= SanitizeFloat(layer.AutoSpeedX, 0.0f);
			changed |= SanitizeFloat(layer.AutoSpeedY, 0.0f);
			return changed;
		}

		// Replaces the parameters of a textured layer that aren't finite by their defaults, returns true if any was
		bool SanitizeTexturedFloats(TexturedLayerParams& params)
		{
			bool changed = SanitizeFloat(params.FadeX, 0.5f);
			changed |= SanitizeFloat(params.FadeY, 0.5f);
			changed |= SanitizeFloat(params.InnerSpeedX, 0.0f);
			changed |= SanitizeFloat(params.InnerSpeedY, 0.0f);
			changed |= SanitizeFloat(params.InnerAutoSpeedX, 0.0f);
			changed |= SanitizeFloat(params.InnerAutoSpeedY, 0.0f);
			return changed;
		}

		// Returns false for a part of a path that Windows would take for another directory or for a device
		bool IsSafePathPart(StringView part)
		{
			// An empty part and the current directory stay where they are
			if (part.empty() || part == "."_s) {
				return true;
			}
			// Windows drops the dots and spaces at the end of a part, so ".. " is the parent directory there, and a part
			// of nothing else could be one of those that go up
			const char last = part[part.size() - 1];
			if (last == '.' || last == ' ') {
				return false;
			}

			// A device name stays a device whatever extension follows it, reading it could block forever
			std::size_t baseLength = 0;
			while (baseLength < part.size() && part[baseLength] != '.') {
				baseLength++;
			}
			while (baseLength > 0 && part[baseLength - 1] == ' ') {
				baseLength--;
			}
			const StringView base = part.prefix(baseLength);
			static const StringView Devices[] = { "CON"_s, "PRN"_s, "AUX"_s, "NUL"_s, "CONIN$"_s, "CONOUT$"_s };
			for (StringView device : Devices) {
				if (StringUtils::equalsIgnoreCase(base, device)) {
					return false;
				}
			}
			if (base.size() == 4 && (StringUtils::equalsIgnoreCase(base.prefix(3), "COM"_s) || StringUtils::equalsIgnoreCase(base.prefix(3), "LPT"_s)) &&
				base[3] >= '0' && base[3] <= '9') {
				return false;
			}
			return true;
		}

		bool IsCriticalChunkKnown(std::uint32_t tag, ArrayView<const std::uint32_t> knownCriticalChunks)
		{
			for (std::uint32_t known : knownCriticalChunks) {
				if (known == tag) {
					return true;
				}
			}
			return false;
		}
	}

	bool LevelFile::IsSafeTileSetPath(StringView path)
	{
		if (path.empty() || path[0] == '/' || path[0] == '\\') {
			return false;
		}

		// Every part between separators is checked, a drive letter, a stream or a scheme isn't a part of a relative path
		// either, nor is a control character
		std::size_t partStart = 0;
		for (std::size_t i = 0; i <= path.size(); i++) {
			if (i < path.size() && (path[i] == ':' || std::uint8_t(path[i]) < 0x20)) {
				return false;
			}
			if (i == path.size() || path[i] == '/' || path[i] == '\\') {
				if (!IsSafePathPart(path.slice(partStart, i))) {
					return false;
				}
				partStart = i + 1;
			}
		}
		return true;
	}

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
		: _header(header), _input(s, header.BodySize), _totalRead(0), _totalLayerTiles(0), _error(nullptr), _bufferPos(0), _bufferLength(0), _failed(false)
	{
	}

	StringView LevelReader::GetError() const
	{
		return (_error != nullptr ? StringView(_error) : StringView());
	}

	void LevelReader::Fail(const char* reason)
	{
		if (!_failed) {
			_error = reason;
			_failed = true;
		}
	}

	bool LevelReader::Refill()
	{
		if (_failed) {
			return false;
		}

		// Whatever is left in the buffer is kept at its start
		std::int32_t left = _bufferLength - _bufferPos;
		if (left < 0) {
			// Never happens, everything that reads from the buffer checks what's left first, but a negative offset
			// would make the read below write in front of the buffer
			Fail("The reader lost its position");
			return false;
		}
		if (left > 0 && _bufferPos > 0) {
			std::memmove(_buffer, _buffer + _bufferPos, left);
		}
		_bufferPos = 0;
		_bufferLength = left;

		// The body inflates a thousand times at most, but a crafted one could keep the reader busy for hours
		if (_totalRead >= LevelFile::MaxBodySize) {
			Fail("The body is too large");
			return false;
		}

		std::int64_t bytesRead = _input.Read(_buffer + _bufferLength, BufferSize - _bufferLength);
		if (bytesRead <= 0) {
			return false;
		}
		_bufferLength += std::int32_t(bytesRead);
		_totalRead += bytesRead;
		return true;
	}

	bool LevelReader::ReadBytes(void* destination, std::int32_t size)
	{
		std::uint8_t* typed = static_cast<std::uint8_t*>(destination);
		while (size > 0) {
			if (_bufferPos >= _bufferLength && !Refill()) {
				// A truncated body reads as zeroes, the caller rejects it by IsValid()
				std::memset(typed, 0, size);
				Fail("The body is truncated");
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
				Fail("The body is truncated");
				return false;
			}
			std::int32_t chunk = std::int32_t(std::min<std::int64_t>(size, _bufferLength - _bufferPos));
			_bufferPos += chunk;
			size -= chunk;
		}
		return true;
	}

	bool LevelReader::ReadBlob(SmallVectorImpl<std::uint8_t>& data, std::uint32_t size)
	{
		// The size alone is never trusted with an allocation, the buffer grows with the data that really comes,
		// so a few bytes claiming a huge blob cost nothing
		constexpr std::uint32_t Step = 64 * 1024;

		data.clear();
		std::uint32_t done = 0;
		while (done < size) {
			const std::uint32_t chunk = std::min<std::uint32_t>(size - done, Step);
			data.resize_for_overwrite(std::size_t(done) + chunk);
			if (!ReadBytes(data.data() + done, std::int32_t(chunk))) {
				data.clear();
				return false;
			}
			done += chunk;
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
		// Five bytes at most, the last one with only the four highest bits of the value; no writer makes a longer
		// one, so such a number is a damaged body rather than something to wade through
		std::uint32_t result = 0;
		for (std::int32_t i = 0; i < 5; i++) {
			std::uint8_t byte = ReadUint8();
			if (_failed) {
				return 0;
			}
			result |= std::uint32_t(byte & 0x7F) << (i * 7);
			if ((byte & 0x80) == 0) {
				if (i == 4 && byte > 0x0F) {
					break;
				}
				return result;
			}
		}
		Fail("A number is too large");
		return 0;
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

	void LevelReader::ReadExtraTileSet(ExtraTileSetRecord& tileSet, bool keepData)
	{
		tileSet.Flags = ReadUint8();
		tileSet.Name = ReadString8();
		tileSet.Offset = ReadUint16();
		tileSet.Count = ReadUint16();
		std::memset(tileSet.Remapping, 0, sizeof(tileSet.Remapping));
		tileSet.AlternatePalette = 0;
		tileSet.Data.clear();

		if ((tileSet.Flags & ~ExtraTileSetRecord::KnownFlags) != 0) {
			// A flag of a later version could bring data of its own, the rest of the body would be misread
			Fail("An extra tile set has a flag this version doesn't know");
			return;
		}

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
			if (size > LevelFile::MaxBlobSize) {
				Fail("An embedded tile set is too large");
				return;
			}
			if (keepData) {
				ReadBlob(tileSet.Data, size);
			} else {
				SkipBytes(size);
			}
		}
	}

	void LevelReader::ReadExtraTileSets(SmallVectorImpl<ExtraTileSetRecord>& tileSets)
	{
		std::uint8_t count = ReadUint8();
		for (std::int32_t i = 0; i < count && !_failed; i++) {
			ReadExtraTileSet(tileSets.emplace_back(), true);
		}
	}

	void LevelReader::ReadTileDiffuseOverrides(SmallVectorImpl<TileDiffuseOverride>* overrides)
	{
		std::uint32_t count = ReadVariableUint32();
		if (count > LevelFile::MaxTileOverrides) {
			Fail("The level has too many overridden tiles");
			return;
		}
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
		if (count > LevelFile::MaxTileOverrides) {
			Fail("The level has too many overridden tile masks");
			return;
		}
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
		// Only LayerNumbersChunk gives it a number
		layer.OriginalNumber = 0;

		if (std::uint8_t(layer.Kind) > std::uint8_t(LayerKind::Sprite)) {
			// The kind decides what follows, a kind of a later version could store anything there
			layer.Kind = LayerKind::Other;
			layer.Width = 0;
			layer.Height = 0;
			Fail("A layer is of a kind this version doesn't know");
		}

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
			// The sprite layer is always placed with its defaults, they aren't stored; it's drawn as ordinary tiles unless
			// the level has a SpriteLayerChunk
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

		// The sizes multiply into memory and into the 32-bit indices of the tiles, so they're bounded well before that
		if (layer.Width < 0 || layer.Height < 0 || layer.Width > LevelFile::MaxLayerSize || layer.Height > LevelFile::MaxLayerSize ||
			std::int64_t(layer.Width) * layer.Height > LevelFile::MaxLayerTiles) {
			layer.Width = 0;
			layer.Height = 0;
			Fail("A layer is too large");
		}
		_totalLayerTiles += std::int64_t(layer.Width) * layer.Height;
		if (_totalLayerTiles > LevelFile::MaxTotalLayerTiles) {
			layer.Width = 0;
			layer.Height = 0;
			Fail("The layers are too large");
		}
		// Nothing would draw such a layer, and the wrapping of its scrolling would never end; 3.9.0 wrote any value
		// it was given, so the level is still read, with the defaults in place of those values
		if (SanitizeLayerFloats(layer)) {
			LOGW("A layer has an invalid offset or speed, the defaults are used instead");
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
			bool truncated = false;

			// The records the buffer already holds whole are decoded straight from it without checking each of them,
			// only the rest of the block (where the buffer ends) goes through the checked loop below
			const std::int32_t direct = std::min<std::int32_t>(count, (_bufferLength - _bufferPos) / TileRecordSize);
			const std::uint8_t* source = &_buffer[_bufferPos];
			for (std::int32_t j = 0; j < direct; j++, source += TileRecordSize) {
				block[j].Flags = source[0];
				block[j].TileId = std::uint16_t(source[1] | (source[2] << 8));
			}
			_bufferPos += direct * TileRecordSize;

			for (std::int32_t j = direct; j < count; j++) {
				if (_bufferLength - _bufferPos < TileRecordSize) {
					// A record can lie across the end of the buffer, the refill keeps its first part at the start, and it
					// can take more than one when the stream hands over its last bytes in pieces
					while (_bufferLength - _bufferPos < TileRecordSize && Refill()) {
					}
					if (_bufferLength - _bufferPos < TileRecordSize) {
						// A truncated stream reads as empty tiles; the caller rejects it by IsValid()
						Fail("The body is truncated");
						std::memset(&block[j], 0, (count - j) * sizeof(TileRecord));
						truncated = true;
						break;
					}
				}
				const std::uint8_t* record = &_buffer[_bufferPos];
				block[j].Flags = record[0];
				block[j].TileId = std::uint16_t(record[1] | (record[2] << 8));
				_bufferPos += TileRecordSize;
			}
			// The layer is limited to far fewer tiles than a 32-bit index holds (see LevelFile::MaxLayerTiles)
			callback(std::int32_t(i), ArrayView<const TileRecord>(block, count));
			if (truncated) {
				break;
			}
		}
	}

	void LevelReader::ReadEvent(EventRecord& event)
	{
		event.Type = ReadUint16();
		// 3.9.0 wrote the bit as it was given, so it means nothing in this version of the layout and is dropped
		event.Flags = std::uint8_t(ReadUint8() & ~EventRecord::Reserved);
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
		for (std::int32_t y = 0; y < height && !_failed; y++) {
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

		// The data of embedded tile sets is skipped rather than kept only to be thrown away
		std::uint8_t extraTileSetCount = ReadUint8();
		for (std::int32_t i = 0; i < extraTileSetCount && !_failed; i++) {
			ExtraTileSetRecord tileSet;
			ReadExtraTileSet(tileSet, false);
		}

		ReadTileDiffuseOverrides(nullptr);
		ReadTileMaskOverrides(nullptr);

		std::uint8_t textCount = ReadUint8();
		for (std::int32_t i = 0; i < textCount && !_failed; i++) {
			SkipBytes(ReadUint16());
		}

		// The frames of the animated tiles are skipped too, 3 bytes each after the timing
		/*std::uint16_t animatedTilesOffset =*/ ReadUint16();
		std::uint16_t animatedTileCount = ReadUint16();
		for (std::int32_t i = 0; i < animatedTileCount && !_failed; i++) {
			std::uint8_t frameCount = ReadUint8();
			SkipBytes(2 + 2 + 2 + 1 + 2 + std::int64_t(frameCount) * 3);
		}

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

		std::uint32_t offGridCount = ReadVariableUint32();
		if (offGridCount > LevelFile::MaxOffGridEvents) {
			Fail("The level has too many off-grid events");
		}
		for (std::uint32_t i = 0; i < offGridCount && !_failed; i++) {
			OffGridEventRecord event;
			event.X = ReadVariableUint32();
			event.Y = ReadVariableUint32();
			ReadEvent(event.Event);
		}
		return !_failed;
	}

	void LevelReader::ReadOffGridEvents(SmallVectorImpl<OffGridEventRecord>& events)
	{
		std::uint32_t count = ReadVariableUint32();
		if (count > LevelFile::MaxOffGridEvents) {
			Fail("The level has too many off-grid events");
			return;
		}
		for (std::uint32_t i = 0; i < count && !_failed; i++) {
			OffGridEventRecord& event = events.emplace_back();
			event.X = ReadVariableUint32();
			event.Y = ReadVariableUint32();
			ReadEvent(event.Event);
		}
	}

	void LevelReader::ReadChunks(SmallVectorImpl<ChunkRecord>& chunks, bool includeEditorOnly, ArrayView<const std::uint32_t> knownCriticalChunks)
	{
		ReadChunksInternal(&chunks, includeEditorOnly, knownCriticalChunks);
	}

	void LevelReader::ReadSelectedChunks(SmallVectorImpl<ChunkRecord>& chunks, Function<bool(std::uint32_t, std::uint32_t)>&& select,
		ArrayView<const std::uint32_t> knownCriticalChunks)
	{
		ReadChunksInternal(&chunks, true, knownCriticalChunks, &select);
	}

	bool LevelReader::SkipChunks(ArrayView<const std::uint32_t> knownCriticalChunks)
	{
		ReadChunksInternal(nullptr, false, knownCriticalChunks);
		return !_failed;
	}

	void LevelReader::ReadChunksInternal(SmallVectorImpl<ChunkRecord>* chunks, bool includeEditorOnly, ArrayView<const std::uint32_t> knownCriticalChunks,
		Function<bool(std::uint32_t, std::uint32_t)>* select)
	{
		if (_failed) {
			return;
		}

		// The chunks are optional, a body that ends here has none
		if (_bufferPos >= _bufferLength && !Refill()) {
			return;
		}

		std::uint32_t count = ReadVariableUint32();
		if (count > MaxChunks) {
			Fail("The level has too many chunks");
			return;
		}
		for (std::uint32_t i = 0; i < count && !_failed; i++) {
			std::uint32_t tag = ReadUint32();
			std::uint32_t size = ReadVariableUint32();
			if (_failed) {
				break;
			}
			if (LevelFile::IsCriticalChunk(tag) && !IsCriticalChunkKnown(tag, knownCriticalChunks)) {
				// The level can't be played correctly without what it holds
				Fail("The level needs a newer version of the game");
				break;
			}
			if (size > LevelFile::MaxBlobSize) {
				Fail("A chunk is too large");
				break;
			}
			if (chunks == nullptr || (!includeEditorOnly && LevelFile::IsEditorOnlyChunk(tag)) ||
				(select != nullptr && *select && !(*select)(tag, size))) {
				SkipBytes(size);
				continue;
			}

			ChunkRecord& chunk = chunks->emplace_back();
			chunk.Tag = tag;
			ReadBlob(chunk.Data, size);
		}
	}

	LevelWriter::LevelWriter(Stream& target, LevelFlags flags, std::int32_t compressionLevel)
		: _target(&target), _flags(flags), _body(1024 * 1024), _layerTilesLeft(0), _totalLayerTiles(0), _eventsLeft(-1), _finished(false), _failed(false), _hasPreview(false)
	{
		// The header is written by Finish() together with the size of the body, every write to the target is checked there
		_deflate = std::make_unique<DeflateWriter>(_body, compressionLevel);
	}

	std::size_t LevelWriter::FitCount(std::size_t count, std::size_t max, const char* what)
	{
		if (count > max) {
			LOGW("Level has {} {}, only the first {} of them are saved", count, what, max);
			return max;
		}
		return count;
	}

	std::size_t LevelWriter::FitUtf8(StringView value, std::size_t max)
	{
		// A sequence the cut falls into is left out whole
		return Death::Utf8::FitPrefix(value, max);
	}

	void LevelWriter::WriteString8(StringView value)
	{
		std::size_t length = FitUtf8(value, UINT8_MAX);
		if (length < value.size()) {
			LOGW("Text \"{}\" is too long, only its first {} bytes are saved", value.prefix(length), length);
		}
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
			if (levelPalette == nullptr) {
				// Written anyway, so the rest of the body stays in place, but such a level isn't completed
				LOGE("The level palette is required by the level flags");
				_failed = true;
			}
			for (std::int32_t i = 0; i < LevelFile::ColorsPerPalette; i++) {
				_deflate->WriteValueAsLE<std::uint32_t>(levelPalette != nullptr ? levelPalette[i] : 0);
			}
		}

		std::size_t count = FitCount(additionalPalettes.size(), UINT8_MAX, "palettes");
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
		std::size_t count = FitCount(tileSets.size(), UINT8_MAX, "extra tile sets");
		_deflate->WriteValue<std::uint8_t>(std::uint8_t(count));
		for (std::size_t i = 0; i < count; i++) {
			const ExtraTileSetRecord& tileSet = tileSets[i];
			// The reserved flags would make the level unreadable
			_deflate->WriteValue<std::uint8_t>(std::uint8_t(tileSet.Flags & ExtraTileSetRecord::KnownFlags));
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
				if (tileSet.Data.size() > LevelFile::MaxBlobSize) {
					// No reader would accept it, an empty one keeps at least the rest of the body in place
					LOGE("Tile set \"{}\" is too large to be stored in the level", tileSet.Name);
					_failed = true;
					_deflate->WriteVariableUint32(0);
				} else {
					_deflate->WriteVariableUint32(std::uint32_t(tileSet.Data.size()));
					_deflate->Write(tileSet.Data.data(), std::int64_t(tileSet.Data.size()));
				}
			}
		}
	}

	void LevelWriter::WriteTileDiffuseOverrides(ArrayView<const TileDiffuseOverride> overrides)
	{
		std::size_t count = FitCount(overrides.size(), LevelFile::MaxTileOverrides, "overridden tiles");
		_deflate->WriteVariableUint32(std::uint32_t(count));
		for (std::size_t i = 0; i < count; i++) {
			_deflate->WriteValueAsLE<std::uint16_t>(overrides[i].TileId);
			_deflate->Write(overrides[i].Indices, sizeof(overrides[i].Indices));
		}
	}

	void LevelWriter::WriteTileMaskOverrides(ArrayView<const TileMaskOverride> overrides)
	{
		std::size_t count = FitCount(overrides.size(), LevelFile::MaxTileOverrides, "overridden tile masks");
		_deflate->WriteVariableUint32(std::uint32_t(count));
		for (std::size_t i = 0; i < count; i++) {
			_deflate->WriteValueAsLE<std::uint16_t>(overrides[i].TileId);
			_deflate->Write(overrides[i].Mask, sizeof(overrides[i].Mask));
		}
	}

	void LevelWriter::WriteTexts(ArrayView<const String> texts)
	{
		std::size_t count = FitCount(texts.size(), UINT8_MAX, "texts");
		_deflate->WriteValue<std::uint8_t>(std::uint8_t(count));
		for (std::size_t i = 0; i < count; i++) {
			const String& text = texts[i];
			std::size_t length = FitUtf8(text, UINT16_MAX);
			if (length < text.size()) {
				LOGW("Text {} is too long, only its first {} bytes are saved", i, length);
			}
			_deflate->WriteValueAsLE<std::uint16_t>(std::uint16_t(length));
			_deflate->Write(text.data(), std::int64_t(length));
		}
	}

	void LevelWriter::WriteAnimatedTiles(std::uint16_t animatedTilesOffset, ArrayView<const AnimatedTileRecord> animatedTiles)
	{
		std::size_t count = FitCount(animatedTiles.size(), UINT16_MAX, "animated tiles");
		_deflate->WriteValueAsLE<std::uint16_t>(animatedTilesOffset);
		_deflate->WriteValueAsLE<std::uint16_t>(std::uint16_t(count));
		for (std::size_t i = 0; i < count; i++) {
			const AnimatedTileRecord& animatedTile = animatedTiles[i];
			std::size_t frameCount = FitCount(animatedTile.Frames.size(), UINT8_MAX, "frames in an animated tile");
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
		if (count < 0 || count > UINT8_MAX) {
			// All the layers are written after it, so a cut count would misplace everything behind them
			LOGE("Level has {} layers, only {} can be saved", count, UINT8_MAX);
			_failed = true;
		}
		_deflate->WriteValue<std::uint8_t>(std::uint8_t(count));
	}

	void LevelWriter::WriteLayerHeader(const LayerRecord& record)
	{
		DEATH_DEBUG_ASSERT(_layerTilesLeft == 0, "Tiles of the previous layer are missing");

		// A reader replaces such values by the defaults, they're written so right away
		LayerRecord layer = record;
		if (layer.Kind != LayerKind::Sprite && SanitizeLayerFloats(layer)) {
			LOGW("Layer has an invalid offset or speed, the defaults are saved instead");
		}

		if (std::uint8_t(layer.Kind) > std::uint8_t(LayerKind::Sprite) || layer.Width < 0 || layer.Height < 0 ||
			layer.Width > LevelFile::MaxLayerSize || layer.Height > LevelFile::MaxLayerSize ||
			std::int64_t(layer.Width) * layer.Height > LevelFile::MaxLayerTiles) {
			// No reader would accept such a layer
			LOGE("Layer of {}x{} tiles can't be saved", layer.Width, layer.Height);
			_failed = true;
		}
		_totalLayerTiles += std::int64_t(layer.Width) * layer.Height;
		if (_totalLayerTiles > LevelFile::MaxTotalLayerTiles && !_failed) {
			LOGE("Layers of the level have too many tiles together to be saved");
			_failed = true;
		}

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

		// The reserved flag would make the level unreadable
		std::uint8_t flags = std::uint8_t(event.Flags & ~(EventRecord::NoParams | EventRecord::Reserved));
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

		std::size_t count = FitCount(events.size(), LevelFile::MaxOffGridEvents, "off-grid events");
		_deflate->WriteVariableUint32(std::uint32_t(count));
		for (std::size_t i = 0; i < count; i++) {
			_deflate->WriteVariableUint32(events[i].X);
			_deflate->WriteVariableUint32(events[i].Y);
			WriteEventFields(events[i].Event);
		}
	}

	void LevelWriter::WriteChunks(ArrayView<const ChunkRecord> chunks, bool stripEditorOnly)
	{
		WriteChunks(chunks, {}, stripEditorOnly);
	}

	void LevelWriter::WriteChunks(ArrayView<const ChunkRecord> first, ArrayView<const ChunkRecord> second, bool stripEditorOnly)
	{
		std::uint32_t count = 0;
		for (ArrayView<const ChunkRecord> chunks : { first, second }) {
			for (const ChunkRecord& chunk : chunks) {
				if (!stripEditorOnly || !LevelFile::IsEditorOnlyChunk(chunk.Tag)) {
					count++;
				}
			}
		}
		if (count == 0) {
			// The list is optional, a level without chunks doesn't store even the count
			return;
		}
		if (count > MaxChunks) {
			LOGE("Level has {} chunks, no reader would accept more than {}", count, MaxChunks);
			_failed = true;
		}

		_deflate->WriteVariableUint32(count);
		for (ArrayView<const ChunkRecord> chunks : { first, second }) {
			for (const ChunkRecord& chunk : chunks) {
				if (stripEditorOnly && LevelFile::IsEditorOnlyChunk(chunk.Tag)) {
					continue;
				}
				_deflate->WriteValueAsLE<std::uint32_t>(chunk.Tag);
				if (chunk.Tag == LevelFile::PreviewChunk && !chunk.Data.empty()) {
					_hasPreview = true;
				}
				if (chunk.Data.size() > LevelFile::MaxBlobSize) {
					// No reader would accept it, an empty one keeps at least the rest of the body in place
					LOGE("Chunk {} is too large to be saved", chunk.Tag);
					_failed = true;
					_deflate->WriteVariableUint32(0);
				} else {
					_deflate->WriteVariableUint32(std::uint32_t(chunk.Data.size()));
					_deflate->Write(chunk.Data.data(), std::int64_t(chunk.Data.size()));
				}
			}
		}
	}

	bool LevelWriter::Finish()
	{
		if (_finished) {
			return false;
		}
		_finished = true;

		// Completes the compressed stream, whose state is known only after that
		_deflate->Dispose();
		bool success = (_deflate->IsValid() && !_failed);
		_deflate = nullptr;

		const std::int64_t bodySize = _body.GetSize();
		if (bodySize < 0 || bodySize > INT32_MAX) {
			LOGE("Level is too large to be saved");
			return false;
		}

		// The header is put together here, so a failed write of any part of the level is noticed
		std::uint8_t header[8 + 1 + 2 + 2 + 4];
		const std::uint64_t signature = LevelFile::Signature;
		for (std::int32_t i = 0; i < 8; i++) {
			header[i] = std::uint8_t(signature >> (i * 8));
		}
		header[8] = std::uint8_t(ContentFileType::Level);
		header[9] = std::uint8_t(LevelFile::CurrentVersion);
		header[10] = std::uint8_t(LevelFile::CurrentVersion >> 8);
		// Whether the level has a preview is known only now, so the flag always tells the truth
		const LevelFlags flags = (_hasPreview ? (_flags | LevelFlags::HasPreview) : (_flags & ~LevelFlags::HasPreview));
		header[11] = std::uint8_t(std::uint16_t(flags));
		header[12] = std::uint8_t(std::uint16_t(flags) >> 8);
		for (std::int32_t i = 0; i < 4; i++) {
			header[13 + i] = std::uint8_t(std::uint32_t(bodySize) >> (i * 8));
		}

		if (_target->Write(header, sizeof(header)) != std::int64_t(sizeof(header)) ||
			_target->Write(_body.GetBuffer(), bodySize) != bodySize || !_target->Flush() || !_target->IsValid()) {
			LOGE("Level cannot be written");
			success = false;
		}
		return success;
	}

	bool SpriteLayerChunk::Read(ArrayView<const std::uint8_t> data, LayerRecord& record)
	{
		// The renderer type and the color, anything after them is of a newer version
		if (data.size() < 1 + sizeof(record.Color)) {
			return false;
		}
		record.RendererType = data[0];
		std::memcpy(record.Color, &data[1], sizeof(record.Color));
		return true;
	}

	void SpriteLayerChunk::Write(const LayerRecord& record, SmallVectorImpl<std::uint8_t>& data)
	{
		data.clear();
		// Ordinary tiles as the reader of the record has them (see LevelReader::ReadLayerHeader())
		static const std::uint8_t White[4] = { 255, 255, 255, 255 };
		if (record.RendererType == 0 && std::memcmp(record.Color, White, sizeof(White)) == 0) {
			return;
		}
		data.push_back(record.RendererType);
		data.append(record.Color, record.Color + sizeof(record.Color));
	}

	bool TexturedLayerChunk::Read(ArrayView<const std::uint8_t> data, SmallVectorImpl<Entry>& layers)
	{
		layers.clear();
		const std::uint8_t* p = data.data();
		const std::uint8_t* end = p + data.size();
		std::uint32_t count;
		// A level has at most 255 layers, see LevelReader::ReadLayerCount()
		if (!ReadVarUint(p, end, count) || count > UINT8_MAX) {
			return false;
		}
		for (std::uint32_t i = 0; i < count; i++) {
			std::uint32_t layer, size;
			if (!ReadVarUint(p, end, layer) || layer > UINT8_MAX || !ReadVarUint(p, end, size) ||
				size < TexturedLayerEntrySize || std::uint32_t(end - p) < size) {
				layers.clear();
				return false;
			}
			Entry& entry = layers.emplace_back();
			entry.Layer = layer;
			entry.Params.FadeX = ReadFloatLE(p);
			entry.Params.FadeY = ReadFloatLE(p + 4);
			entry.Params.Fade = p[8];
			entry.Params.Flags = p[9];
			// The inner speeds and the texture, an entry without them has the defaults
			if (size >= TexturedLayerTextureEntrySize) {
				entry.Params.InnerSpeedX = ReadFloatLE(p + 10);
				entry.Params.InnerSpeedY = ReadFloatLE(p + 14);
				entry.Params.InnerAutoSpeedX = ReadFloatLE(p + 18);
				entry.Params.InnerAutoSpeedY = ReadFloatLE(p + 22);
				// A texture of a kind this version doesn't know leaves the tiles of the layer
				if (p[26] == TexturedLayerTextureImage) {
					if (size - TexturedLayerTextureEntrySize < TexturedLayerTextureBytes) {
						layers.clear();
						return false;
					}
					const std::uint8_t* image = p + TexturedLayerTextureEntrySize;
					entry.Texture.assign(image, image + TexturedLayerTextureBytes);
				}
			}
			// A value that isn't finite has the default instead, as an offset or a speed of the layer itself
			if (SanitizeTexturedFloats(entry.Params)) {
				LOGW("Layer {} has invalid parameters of its texture, the defaults are used instead", layer);
			}
			// What a newer version added is skipped
			p += size;
		}
		return true;
	}

	void TexturedLayerChunk::Write(ArrayView<const Entry> layers, SmallVectorImpl<std::uint8_t>& data)
	{
		data.clear();
		auto hasTexture = [](const Entry& entry) {
			return (entry.Texture.size() == TexturedLayerTextureBytes);
		};
		std::uint32_t count = 0;
		for (const Entry& entry : layers) {
			if ((!entry.Params.IsDefault() || hasTexture(entry)) && entry.Layer <= UINT8_MAX) {
				count++;
			}
		}
		if (count == 0) {
			// Every layer has the defaults, no chunk
			return;
		}

		WriteVarUint(data, count);
		for (const Entry& entry : layers) {
			if ((entry.Params.IsDefault() && !hasTexture(entry)) || entry.Layer > UINT8_MAX) {
				continue;
			}
			// A reader replaces values that aren't finite by the defaults, they're written so right away
			TexturedLayerParams params = entry.Params;
			if (SanitizeTexturedFloats(params)) {
				LOGW("Layer {} has invalid parameters of its texture, the defaults are saved instead", entry.Layer);
			}
			// As short as the parameters allow, so a layer without inner speeds and a texture of its own is written as before
			const bool extended = (!params.HasDefaultInnerSpeeds() || hasTexture(entry));
			WriteVarUint(data, entry.Layer);
			WriteVarUint(data, extended ? TexturedLayerTextureEntrySize + (hasTexture(entry) ? TexturedLayerTextureBytes : 0) : TexturedLayerEntrySize);
			WriteFloatLE(data, params.FadeX);
			WriteFloatLE(data, params.FadeY);
			data.push_back(params.Fade);
			data.push_back(params.Flags);
			if (extended) {
				WriteFloatLE(data, params.InnerSpeedX);
				WriteFloatLE(data, params.InnerSpeedY);
				WriteFloatLE(data, params.InnerAutoSpeedX);
				WriteFloatLE(data, params.InnerAutoSpeedY);
				if (hasTexture(entry)) {
					data.push_back(TexturedLayerTextureImage);
					data.append(entry.Texture.begin(), entry.Texture.end());
				} else {
					data.push_back(TexturedLayerTextureTiles);
				}
			}
		}
	}

	StringView LayerNamesChunk::Read(ArrayView<const std::uint8_t> data, std::int32_t layer)
	{
		std::size_t pos = 0;
		for (std::int32_t i = 0; pos < data.size(); i++) {
			// The length byte first, the rest of the chunk is measured after it
			std::size_t length = data[pos++];
			length = std::min<std::size_t>(length, data.size() - pos);
			if (i == layer) {
				return StringView(reinterpret_cast<const char*>(data.data() + pos), length);
			}
			pos += length;
		}
		return {};
	}

	void LayerNamesChunk::Write(ArrayView<const StringView> names, SmallVectorImpl<std::uint8_t>& data)
	{
		data.clear();
		if (std::none_of(names.begin(), names.end(), [](StringView name) { return !name.empty(); })) {
			return;
		}
		for (StringView name : names) {
			// Not in the middle of a character
			std::size_t length = Death::Utf8::FitPrefix(name, MaxNameLength);
			data.push_back(std::uint8_t(length));
			data.append(reinterpret_cast<const std::uint8_t*>(name.data()), reinterpret_cast<const std::uint8_t*>(name.data()) + length);
		}
	}

	std::uint8_t LayerNumbersChunk::Read(ArrayView<const std::uint8_t> data, std::int32_t layer)
	{
		if (layer < 0 || std::size_t(layer) >= data.size()) {
			return 0;
		}
		std::uint8_t number = data[layer];
		return (number >= 1 && number <= OriginalLayerCount ? number : 0);
	}

	void LayerNumbersChunk::Write(ArrayView<const std::uint8_t> numbers, SmallVectorImpl<std::uint8_t>& data)
	{
		data.clear();
		if (std::none_of(numbers.begin(), numbers.end(), [](std::uint8_t number) { return number != 0; })) {
			return;
		}
		for (std::uint8_t number : numbers) {
			data.push_back(number >= 1 && number <= OriginalLayerCount ? number : 0);
		}
	}
}
