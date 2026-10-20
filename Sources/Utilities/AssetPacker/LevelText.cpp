#include "LevelText.h"

#include "../../Jazz2/Formats/LevelFile.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>
#include <IO/MemoryStream.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::IO;
using namespace Jazz2::Formats;

namespace Jazz2::AssetPacker
{
	namespace
	{
		constexpr std::int32_t FormatVersion = 1;

		struct LayerData {
			LayerRecord Header;
			SmallVector<TileRecord, 0> Tiles;
		};

		/** Everything a level file holds, as the records the reader returns */
		struct LevelData {
			LevelHeader Header;
			LevelProperties Properties;
			bool HasLevelPalette = false;
			std::uint32_t LevelPalette[LevelFile::ColorsPerPalette];
			SmallVector<PaletteRecord, 0> AdditionalPalettes;
			SmallVector<ExtraTileSetRecord, 0> ExtraTileSets;
			SmallVector<TileDiffuseOverride, 0> DiffuseOverrides;
			SmallVector<TileMaskOverride, 0> MaskOverrides;
			SmallVector<String, 0> Texts;
			std::uint16_t AnimatedTilesOffset = 0;
			SmallVector<AnimatedTileRecord, 0> AnimatedTiles;
			SmallVector<LayerData, 0> Layers;
			SmallVector<EventRecord, 0> Events;
			SmallVector<OffGridEventRecord, 0> OffGridEvents;
			SmallVector<ChunkRecord, 0> Chunks;
		};

		bool ReadLevel(const std::uint8_t* data, std::int64_t size, LevelData& level, StringView path)
		{
			MemoryStream input(data, size);
			if (!LevelFile::ReadHeader(input, level.Header)) {
				LOGE("\"{}\" isn't a level of the game's own format", path);
				return false;
			}

			LevelReader reader(input, level.Header);
			reader.ReadProperties(level.Properties);
			level.HasLevelPalette = reader.ReadPalettes(level.LevelPalette, &level.AdditionalPalettes);
			reader.ReadExtraTileSets(level.ExtraTileSets);
			reader.ReadTileDiffuseOverrides(&level.DiffuseOverrides);
			reader.ReadTileMaskOverrides(&level.MaskOverrides);
			reader.ReadTexts(level.Texts);
			reader.ReadAnimatedTiles(level.AnimatedTilesOffset, level.AnimatedTiles);

			std::int32_t layerCount = reader.ReadLayerCount();
			// By index, a pointer into the list would be left behind as it grows
			std::int32_t spriteLayer = -1;
			for (std::int32_t i = 0; i < layerCount && reader.IsValid(); i++) {
				LayerData& layer = level.Layers.emplace_back();
				reader.ReadLayerHeader(layer.Header);
				if (!reader.IsValid()) {
					break;
				}
				layer.Tiles.resize_for_overwrite(std::size_t(layer.Header.Width) * std::size_t(layer.Header.Height));
				reader.ReadLayerTiles(layer.Header, [&layer](std::int32_t first, ArrayView<const TileRecord> tiles) {
					std::memcpy(&layer.Tiles[first], tiles.data(), tiles.size() * sizeof(TileRecord));
				});
				if (layer.Header.Kind == LayerKind::Sprite) {
					spriteLayer = i;
				}
			}
			if (spriteLayer >= 0) {
				const LayerRecord& header = level.Layers[spriteLayer].Header;
				reader.ReadEvents(header.Width, header.Height, [&level](std::int32_t, std::int32_t, const EventRecord& event) {
					level.Events.push_back(event);
				});
			}
			reader.ReadOffGridEvents(level.OffGridEvents);
			reader.ReadChunks(level.Chunks, true);

			if (!reader.IsValid()) {
				LOGE("Level \"{}\" cannot be read: {}", path, reader.GetError());
				return false;
			}
			if (spriteLayer < 0) {
				LOGE("Level \"{}\" cannot be read: It has no sprite layer", path);
				return false;
			}
			return true;
		}

		bool WriteLevel(Stream& output, const LevelData& level)
		{
			LevelWriter writer(output, level.Header.Flags);
			writer.WriteProperties(level.Properties);
			writer.WritePalettes(level.HasLevelPalette ? level.LevelPalette : nullptr, level.AdditionalPalettes);
			writer.WriteExtraTileSets(level.ExtraTileSets);
			writer.WriteTileDiffuseOverrides(level.DiffuseOverrides);
			writer.WriteTileMaskOverrides(level.MaskOverrides);
			writer.WriteTexts(level.Texts);
			writer.WriteAnimatedTiles(level.AnimatedTilesOffset, level.AnimatedTiles);
			writer.WriteLayerCount(std::int32_t(level.Layers.size()));
			for (const LayerData& layer : level.Layers) {
				writer.WriteLayerHeader(layer.Header);
				writer.WriteLayerTiles(layer.Tiles);
			}
			for (const EventRecord& event : level.Events) {
				writer.WriteEvent(event);
			}
			writer.WriteOffGridEvents(level.OffGridEvents);
			writer.WriteChunks(level.Chunks, false);
			return writer.Finish();
		}

		// An event the editor doesn't store, as most tiles of the grid have it
		EventRecord MakeEmptyEvent()
		{
			EventRecord event;
			std::memset(&event, 0, sizeof(event));
			event.Flags = EventRecord::DifficultyMask;
			return event;
		}

		bool IsEmptyEvent(const EventRecord& event)
		{
			// NoParams is derived by the writer from the parameters, so it isn't a difference
			static const EventRecord empty = MakeEmptyEvent();
			return (event.Type == empty.Type && (event.Flags & ~EventRecord::NoParams) == empty.Flags &&
				event.GeneratorFlags == 0 && event.GeneratorDelay == 0 && std::memcmp(event.Params, empty.Params, sizeof(event.Params)) == 0);
		}

		const LayerData* FindSpriteLayer(const LevelData& level)
		{
			const LayerData* spriteLayer = nullptr;
			// The last one wins, as in the game
			for (const LayerData& layer : level.Layers) {
				if (layer.Header.Kind == LayerKind::Sprite) {
					spriteLayer = &layer;
				}
			}
			return spriteLayer;
		}

		/** Builds the text */
		class TextWriter
		{
		public:
			void Raw(StringView text) {
				_data.append(text.begin(), text.end());
			}
			void Char(char c) {
				_data.push_back(c);
			}
			void Line(StringView text) {
				Raw(text);
				Char('\n');
			}
			void Format(const char* format, ...) {
				char buffer[256];
				va_list args;
				va_start(args, format);
				std::int32_t length = std::vsnprintf(buffer, sizeof(buffer), format, args);
				va_end(args);
				if (length > 0) {
					_data.append(buffer, buffer + std::min<std::int32_t>(length, std::int32_t(sizeof(buffer)) - 1));
				}
			}
			void Quoted(StringView value) {
				Char('"');
				for (char c : value) {
					switch (c) {
						case '"': Raw("\\\""_s); break;
						case '\\': Raw("\\\\"_s); break;
						case '\n': Raw("\\n"_s); break;
						case '\r': Raw("\\r"_s); break;
						case '\t': Raw("\\t"_s); break;
						case '\f': Raw("\\f"_s); break;
						default:
							if (std::uint8_t(c) < 0x20 || c == 0x7F) {
								Format("\\x%02X", std::uint8_t(c));
							} else {
								Char(c);
							}
							break;
					}
				}
				Char('"');
			}
			void Float(float value) {
				if (std::isfinite(value)) {
					// Nine significant digits are enough for any float to be read back exactly
					Format("%.9g", value);
				} else {
					std::uint32_t bits;
					std::memcpy(&bits, &value, sizeof(bits));
					Format("bits:%08X", bits);
				}
			}
			void Tile(const TileRecord& tile) {
				if (tile.Flags != 0) {
					Format("%u:%X", std::uint32_t(tile.TileId), std::uint32_t(tile.Flags));
				} else {
					Format("%u", std::uint32_t(tile.TileId));
				}
			}
			void HexBytes(const std::uint8_t* data, std::size_t size) {
				static const char Digits[] = "0123456789ABCDEF";
				for (std::size_t i = 0; i < size; i++) {
					Char(Digits[data[i] >> 4]);
					Char(Digits[data[i] & 0x0F]);
				}
			}
			void Base64(ArrayView<const std::uint8_t> data) {
				static const char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
				// 57 bytes make a line of 76 characters
				for (std::size_t lineStart = 0; lineStart < data.size(); lineStart += 57) {
					std::size_t lineEnd = std::min<std::size_t>(lineStart + 57, data.size());
					for (std::size_t i = lineStart; i < lineEnd; i += 3) {
						std::uint32_t chunk = std::uint32_t(data[i]) << 16;
						std::size_t left = lineEnd - i;
						if (left > 1) chunk |= std::uint32_t(data[i + 1]) << 8;
						if (left > 2) chunk |= std::uint32_t(data[i + 2]);
						Char(Alphabet[(chunk >> 18) & 0x3F]);
						Char(Alphabet[(chunk >> 12) & 0x3F]);
						Char(left > 1 ? Alphabet[(chunk >> 6) & 0x3F] : '=');
						Char(left > 2 ? Alphabet[chunk & 0x3F] : '=');
					}
					Char('\n');
				}
				Line("end"_s);
			}

			ArrayView<const char> GetData() const {
				return _data;
			}

		private:
			SmallVector<char, 0> _data;
		};

		void WriteText(TextWriter& w, const LevelData& level)
		{
			w.Line("# Level of Jazz\xC2\xB2 Resurrection as text, AssetPacker \"text-to-level\" makes the level of it again"_s);
			w.Format("format %d\n", FormatVersion);
			w.Format("flags 0x%04X\n", std::uint32_t(level.Header.Flags));

			const LevelProperties& p = level.Properties;
			w.Raw("name "_s); w.Quoted(p.DisplayName); w.Char('\n');
			w.Raw("author "_s); w.Quoted(p.Author); w.Char('\n');
			w.Raw("next "_s); w.Quoted(p.NextLevel); w.Char('\n');
			w.Raw("secret "_s); w.Quoted(p.SecretLevel); w.Char('\n');
			w.Raw("bonus "_s); w.Quoted(p.BonusLevel); w.Char('\n');
			w.Raw("tileset "_s); w.Quoted(p.DefaultTileSet); w.Char('\n');
			w.Raw("music "_s); w.Quoted(p.Music); w.Char('\n');
			w.Format("ambient 0x%08X\n", p.AmbientColor);
			w.Format("weather %u %u\n", std::uint32_t(p.Weather), std::uint32_t(p.WeatherIntensity));
			w.Format("water %u\n", std::uint32_t(p.WaterLevel));
			w.Format("caption %u\n", std::uint32_t(p.CaptionTileId));

			auto writePalette = [&w](const std::uint32_t* colors) {
				for (std::int32_t row = 0; row < 16; row++) {
					for (std::int32_t i = 0; i < 16; i++) {
						w.Format(i > 0 ? " %08X" : "%08X", colors[row * 16 + i]);
					}
					w.Char('\n');
				}
			};
			if (level.HasLevelPalette) {
				w.Line("palette"_s);
				writePalette(level.LevelPalette);
			}
			for (const PaletteRecord& palette : level.AdditionalPalettes) {
				w.Raw("extra-palette "_s); w.Quoted(palette.Name); w.Char('\n');
				writePalette(palette.Colors);
			}

			for (const ExtraTileSetRecord& tileSet : level.ExtraTileSets) {
				w.Format("extra-tileset 0x%02X ", std::uint32_t(tileSet.Flags));
				w.Quoted(tileSet.Name);
				w.Format(" %u %u %u\n", std::uint32_t(tileSet.Offset), std::uint32_t(tileSet.Count), std::uint32_t(tileSet.AlternatePalette));
				// Stored only for an 8-bit tile set remapped to the level palette, as the reader has it
				if ((tileSet.Flags & ExtraTileSetRecord::Remapped) != 0 && (tileSet.Flags & ExtraTileSetRecord::TrueColor) == 0) {
					w.Line("remap"_s);
					for (std::int32_t row = 0; row < 16; row++) {
						w.HexBytes(&tileSet.Remapping[row * 16], 16);
						w.Char('\n');
					}
				}
				if ((tileSet.Flags & ExtraTileSetRecord::Embedded) != 0) {
					w.Line("data"_s);
					w.Base64(tileSet.Data);
				}
			}

			constexpr std::int32_t TileSize = LevelFile::TileSize;
			for (const TileDiffuseOverride& tile : level.DiffuseOverrides) {
				w.Format("diffuse %u\n", std::uint32_t(tile.TileId));
				for (std::int32_t y = 0; y < TileSize; y++) {
					w.HexBytes(&tile.Indices[y * TileSize], TileSize);
					w.Char('\n');
				}
			}
			for (const TileMaskOverride& tile : level.MaskOverrides) {
				w.Format("mask %u\n", std::uint32_t(tile.TileId));
				for (std::int32_t y = 0; y < TileSize; y++) {
					w.HexBytes(&tile.Mask[y * TileSize], TileSize);
					w.Char('\n');
				}
			}

			for (const String& text : level.Texts) {
				w.Raw("text "_s); w.Quoted(text); w.Char('\n');
			}

			w.Format("animated-offset %u\n", std::uint32_t(level.AnimatedTilesOffset));
			for (const AnimatedTileRecord& animation : level.AnimatedTiles) {
				w.Format("animation %u %u %u %u %u\n", std::uint32_t(animation.FrameDuration16), std::uint32_t(animation.Delay),
					std::uint32_t(animation.DelayJitter), std::uint32_t(animation.PingPongDelay), std::uint32_t(animation.IsPingPong));
				w.Raw("frames"_s);
				for (const TileRecord& frame : animation.Frames) {
					w.Char(' ');
					w.Tile(frame);
				}
				w.Char('\n');
			}

			for (const LayerData& layer : level.Layers) {
				const LayerRecord& r = layer.Header;
				w.Format("layer %u 0x%04X %d %d 0x%02X ", std::uint32_t(r.Kind), std::uint32_t(r.Flags), r.Width, r.Height, std::uint32_t(r.SpeedModels));
				for (float value : { r.OffsetX, r.OffsetY, r.SpeedX, r.SpeedY, r.AutoSpeedX, r.AutoSpeedY }) {
					w.Float(value);
					w.Char(' ');
				}
				w.Format("%d %u %u %u %u %u\n", std::int32_t(r.Depth), std::uint32_t(r.RendererType), std::uint32_t(r.Color[0]),
					std::uint32_t(r.Color[1]), std::uint32_t(r.Color[2]), std::uint32_t(r.Color[3]));
				// A row of the layer on each line, so a change of a tile is a change of a line. A layer with no width has no
				// rows to write, an empty line would be skipped when reading.
				const std::int32_t rowCount = (r.Width > 0 ? r.Height : 0);
				for (std::int32_t y = 0; y < rowCount; y++) {
					for (std::int32_t x = 0; x < r.Width; x++) {
						if (x > 0) {
							w.Char(' ');
						}
						w.Tile(layer.Tiles[std::size_t(y) * std::size_t(r.Width) + std::size_t(x)]);
					}
					w.Char('\n');
				}
			}

			auto writeEvent = [&w](const EventRecord& event) {
				w.Format(" %u 0x%02X %u %u ", std::uint32_t(event.Type), std::uint32_t(event.Flags & ~EventRecord::NoParams),
					std::uint32_t(event.GeneratorFlags), std::uint32_t(event.GeneratorDelay));
				w.HexBytes(event.Params, sizeof(event.Params));
				w.Char('\n');
			};
			const LayerData* spriteLayer = FindSpriteLayer(level);
			std::int32_t gridWidth = (spriteLayer != nullptr ? spriteLayer->Header.Width : 0);
			for (std::size_t i = 0; i < level.Events.size(); i++) {
				if (!IsEmptyEvent(level.Events[i]) && gridWidth > 0) {
					w.Format("event %d %d", std::int32_t(i % std::size_t(gridWidth)), std::int32_t(i / std::size_t(gridWidth)));
					writeEvent(level.Events[i]);
				}
			}
			for (const OffGridEventRecord& event : level.OffGridEvents) {
				w.Format("offgrid %u %u", event.X, event.Y);
				writeEvent(event.Event);
			}

			for (const ChunkRecord& chunk : level.Chunks) {
				char tag[5];
				for (std::int32_t i = 0; i < 4; i++) {
					char c = char((chunk.Tag >> (i * 8)) & 0xFF);
					tag[i] = (c > 0x20 && c < 0x7F ? c : '?');
				}
				tag[4] = '\0';
				w.Format("chunk 0x%08X %s\n", chunk.Tag, tag);
				w.Base64(chunk.Data);
			}
		}

		/** Reads the text line by line, each split into tokens */
		class TextReader
		{
		public:
			TextReader(StringView text, StringView path)
				: _text(text), _path(path), _pos(0), _line(0), _failed(false) {}

			bool IsValid() const {
				return !_failed;
			}

			// Moves to the next line that isn't empty or a comment, returns false at the end of the text
			bool NextLine() {
				while (_pos < _text.size()) {
					std::size_t end = _pos;
					while (end < _text.size() && _text[end] != '\n') {
						end++;
					}
					_current = _text.slice(_pos, end);
					if (!_current.empty() && _current.back() == '\r') {
						_current = _current.exceptSuffix(1);
					}
					_pos = (end < _text.size() ? end + 1 : end);
					_line++;
					_cursor = 0;
					SkipSpaces();
					if (_cursor < _current.size() && _current[_cursor] != '#') {
						return true;
					}
				}
				return false;
			}

			bool AtLineEnd() {
				SkipSpaces();
				return (_cursor >= _current.size());
			}

			StringView Word() {
				SkipSpaces();
				std::size_t start = _cursor;
				while (_cursor < _current.size() && _current[_cursor] != ' ' && _current[_cursor] != '\t') {
					_cursor++;
				}
				return _current.slice(start, _cursor);
			}

			std::int64_t Integer() {
				StringView word = Word();
				if (word.empty()) {
					Fail("A number is missing");
					return 0;
				}
				String copy = word;
				char* end;
				std::int64_t value = std::strtoll(copy.data(), &end, 0);
				if (*end != '\0') {
					Fail("A number is damaged");
				}
				return value;
			}

			template<typename T>
			T Unsigned(std::int64_t max) {
				std::int64_t value = Integer();
				if (value < 0 || value > max) {
					Fail("A number is out of its range");
					return T(0);
				}
				return T(value);
			}

			float Float() {
				StringView word = Word();
				if (word.hasPrefix("bits:"_s)) {
					String copy = word.exceptPrefix(5);
					std::uint32_t bits = std::uint32_t(std::strtoul(copy.data(), nullptr, 16));
					float value;
					std::memcpy(&value, &bits, sizeof(value));
					return value;
				}
				String copy = word;
				char* end;
				float value = std::strtof(copy.data(), &end);
				if (word.empty() || *end != '\0') {
					Fail("A number is damaged");
				}
				return value;
			}

			String Quoted() {
				SkipSpaces();
				if (_cursor >= _current.size() || _current[_cursor] != '"') {
					Fail("A quoted text is missing");
					return {};
				}
				_cursor++;
				String result;
				SmallVector<char, 64> buffer;
				while (_cursor < _current.size() && _current[_cursor] != '"') {
					char c = _current[_cursor++];
					if (c == '\\' && _cursor < _current.size()) {
						char e = _current[_cursor++];
						switch (e) {
							case 'n': buffer.push_back('\n'); break;
							case 'r': buffer.push_back('\r'); break;
							case 't': buffer.push_back('\t'); break;
							case 'f': buffer.push_back('\f'); break;
							case 'x': {
								if (_cursor + 2 > _current.size()) {
									Fail("An escape is damaged");
									return {};
								}
								String hex = _current.slice(_cursor, _cursor + 2);
								buffer.push_back(char(std::strtoul(hex.data(), nullptr, 16)));
								_cursor += 2;
								break;
							}
							default: buffer.push_back(e); break;
						}
					} else {
						buffer.push_back(c);
					}
				}
				if (_cursor >= _current.size()) {
					Fail("A quoted text isn't closed");
					return {};
				}
				_cursor++;
				return String(buffer.data(), buffer.size());
			}

			TileRecord Tile(StringView word) {
				TileRecord tile;
				tile.Flags = 0;
				tile.TileId = 0;
				std::size_t colon = 0;
				while (colon < word.size() && word[colon] != ':') {
					colon++;
				}
				String id = word.prefix(colon);
				char* end;
				unsigned long value = std::strtoul(id.data(), &end, 10);
				if (id.empty() || *end != '\0' || value > UINT16_MAX) {
					Fail("A tile is damaged");
					return tile;
				}
				tile.TileId = std::uint16_t(value);
				if (colon < word.size()) {
					String flags = word.exceptPrefix(colon + 1);
					unsigned long flagValue = std::strtoul(flags.data(), &end, 16);
					if (flags.empty() || *end != '\0' || flagValue > UINT8_MAX) {
						Fail("The flags of a tile are damaged");
						return tile;
					}
					tile.Flags = std::uint8_t(flagValue);
				}
				return tile;
			}

			bool HexBytes(std::uint8_t* data, std::size_t size) {
				StringView word = Word();
				if (word.size() != size * 2) {
					Fail("A line of bytes has a wrong length");
					return false;
				}
				for (std::size_t i = 0; i < size; i++) {
					std::int32_t high = HexDigit(word[i * 2]);
					std::int32_t low = HexDigit(word[i * 2 + 1]);
					if (high < 0 || low < 0) {
						Fail("A line of bytes is damaged");
						return false;
					}
					data[i] = std::uint8_t((high << 4) | low);
				}
				return true;
			}

			// Lines of Base64 up to "end"
			bool Base64(SmallVectorImpl<std::uint8_t>& data) {
				data.clear();
				while (true) {
					if (!NextLine()) {
						Fail("Base64 data isn't closed by \"end\"");
						return false;
					}
					StringView line = _current.exceptPrefix(_cursor).trimmed();
					if (line == "end"_s) {
						return true;
					}
					std::uint32_t chunk = 0;
					std::int32_t bits = 0;
					for (char c : line) {
						std::int32_t value = Base64Digit(c);
						if (c == '=') {
							break;
						}
						if (value < 0) {
							Fail("Base64 data is damaged");
							return false;
						}
						chunk = (chunk << 6) | std::uint32_t(value);
						bits += 6;
						if (bits >= 8) {
							bits -= 8;
							data.push_back(std::uint8_t(chunk >> bits));
						}
					}
				}
			}

			void Fail(const char* message) {
				if (!_failed) {
					LOGE("\"{}\", line {}: {}", _path, _line, message);
				}
				_failed = true;
			}

		private:
			StringView _text;
			StringView _path;
			StringView _current;
			std::size_t _pos;
			std::size_t _cursor = 0;
			std::int32_t _line;
			bool _failed;

			void SkipSpaces() {
				while (_cursor < _current.size() && (_current[_cursor] == ' ' || _current[_cursor] == '\t')) {
					_cursor++;
				}
			}

			static std::int32_t HexDigit(char c) {
				if (c >= '0' && c <= '9') return c - '0';
				if (c >= 'A' && c <= 'F') return c - 'A' + 10;
				if (c >= 'a' && c <= 'f') return c - 'a' + 10;
				return -1;
			}

			static std::int32_t Base64Digit(char c) {
				if (c >= 'A' && c <= 'Z') return c - 'A';
				if (c >= 'a' && c <= 'z') return c - 'a' + 26;
				if (c >= '0' && c <= '9') return c - '0' + 52;
				if (c == '+') return 62;
				if (c == '/') return 63;
				return -1;
			}
		};

		bool ParseText(TextReader& r, LevelData& level)
		{
			level.Header.Flags = LevelFlags::None;
			level.Header.Version = LevelFile::CurrentVersion;
			level.Header.BodySize = 0;
			LevelProperties& p = level.Properties;
			p.AmbientColor = 0xFFFFFFFFu;
			p.Weather = 0;
			p.WeatherIntensity = 0;
			p.WaterLevel = UINT16_MAX;
			p.CaptionTileId = 0;

			auto readPalette = [&r](std::uint32_t* colors) {
				for (std::int32_t row = 0; row < 16 && r.IsValid(); row++) {
					if (!r.NextLine()) {
						r.Fail("A palette is cut short");
						return;
					}
					for (std::int32_t i = 0; i < 16 && r.IsValid(); i++) {
						StringView word = r.Word();
						String copy = word;
						char* end;
						colors[row * 16 + i] = std::uint32_t(std::strtoul(copy.data(), &end, 16));
						if (word.empty() || *end != '\0') {
							r.Fail("A color of a palette is damaged");
						}
					}
				}
			};
			auto readEvent = [&r](EventRecord& event) {
				event.Type = r.Unsigned<std::uint16_t>(UINT16_MAX);
				event.Flags = r.Unsigned<std::uint8_t>(UINT8_MAX);
				event.GeneratorFlags = r.Unsigned<std::uint8_t>(UINT8_MAX);
				event.GeneratorDelay = r.Unsigned<std::uint8_t>(UINT8_MAX);
				r.HexBytes(event.Params, sizeof(event.Params));
			};

			bool hasFormat = false;
			bool eventsStarted = false;
			while (r.IsValid() && r.NextLine()) {
				StringView keyword = r.Word();
				if (keyword == "format"_s) {
					std::int64_t version = r.Integer();
					if (version < 1 || version > FormatVersion) {
						r.Fail("The text is of a newer version");
						return false;
					}
					hasFormat = true;
				} else if (!hasFormat) {
					r.Fail("The text doesn't start with \"format\"");
					return false;
				} else if (keyword == "flags"_s) {
					level.Header.Flags = LevelFlags(r.Unsigned<std::uint16_t>(UINT16_MAX));
				} else if (keyword == "name"_s) {
					p.DisplayName = r.Quoted();
				} else if (keyword == "author"_s) {
					p.Author = r.Quoted();
				} else if (keyword == "next"_s) {
					p.NextLevel = r.Quoted();
				} else if (keyword == "secret"_s) {
					p.SecretLevel = r.Quoted();
				} else if (keyword == "bonus"_s) {
					p.BonusLevel = r.Quoted();
				} else if (keyword == "tileset"_s) {
					p.DefaultTileSet = r.Quoted();
				} else if (keyword == "music"_s) {
					p.Music = r.Quoted();
				} else if (keyword == "ambient"_s) {
					p.AmbientColor = r.Unsigned<std::uint32_t>(UINT32_MAX);
				} else if (keyword == "weather"_s) {
					p.Weather = r.Unsigned<std::uint8_t>(UINT8_MAX);
					p.WeatherIntensity = r.Unsigned<std::uint8_t>(UINT8_MAX);
				} else if (keyword == "water"_s) {
					p.WaterLevel = r.Unsigned<std::uint16_t>(UINT16_MAX);
				} else if (keyword == "caption"_s) {
					p.CaptionTileId = r.Unsigned<std::uint16_t>(UINT16_MAX);
				} else if (keyword == "palette"_s) {
					level.HasLevelPalette = true;
					readPalette(level.LevelPalette);
				} else if (keyword == "extra-palette"_s) {
					PaletteRecord& palette = level.AdditionalPalettes.emplace_back();
					palette.Name = r.Quoted();
					readPalette(palette.Colors);
				} else if (keyword == "extra-tileset"_s) {
					ExtraTileSetRecord& tileSet = level.ExtraTileSets.emplace_back();
					std::memset(tileSet.Remapping, 0, sizeof(tileSet.Remapping));
					tileSet.Flags = r.Unsigned<std::uint8_t>(UINT8_MAX);
					tileSet.Name = r.Quoted();
					tileSet.Offset = r.Unsigned<std::uint16_t>(UINT16_MAX);
					tileSet.Count = r.Unsigned<std::uint16_t>(UINT16_MAX);
					tileSet.AlternatePalette = r.Unsigned<std::uint8_t>(UINT8_MAX);
				} else if (keyword == "remap"_s) {
					if (level.ExtraTileSets.empty()) {
						r.Fail("\"remap\" isn't after an extra tile set");
						return false;
					}
					ExtraTileSetRecord& tileSet = level.ExtraTileSets.back();
					for (std::int32_t row = 0; row < 16 && r.IsValid(); row++) {
						if (!r.NextLine()) {
							r.Fail("A remapping is cut short");
							return false;
						}
						r.HexBytes(&tileSet.Remapping[row * 16], 16);
					}
				} else if (keyword == "data"_s) {
					if (level.ExtraTileSets.empty()) {
						r.Fail("\"data\" isn't after an extra tile set");
						return false;
					}
					r.Base64(level.ExtraTileSets.back().Data);
				} else if (keyword == "diffuse"_s || keyword == "mask"_s) {
					constexpr std::int32_t TileSize = LevelFile::TileSize;
					std::uint16_t tileId = r.Unsigned<std::uint16_t>(UINT16_MAX);
					std::uint8_t* pixels;
					if (keyword == "diffuse"_s) {
						TileDiffuseOverride& tile = level.DiffuseOverrides.emplace_back();
						tile.TileId = tileId;
						pixels = tile.Indices;
					} else {
						TileMaskOverride& tile = level.MaskOverrides.emplace_back();
						tile.TileId = tileId;
						pixels = tile.Mask;
					}
					for (std::int32_t y = 0; y < TileSize && r.IsValid(); y++) {
						if (!r.NextLine()) {
							r.Fail("A tile is cut short");
							return false;
						}
						r.HexBytes(&pixels[y * TileSize], TileSize);
					}
				} else if (keyword == "text"_s) {
					level.Texts.push_back(r.Quoted());
				} else if (keyword == "animated-offset"_s) {
					level.AnimatedTilesOffset = r.Unsigned<std::uint16_t>(UINT16_MAX);
				} else if (keyword == "animation"_s) {
					AnimatedTileRecord& animation = level.AnimatedTiles.emplace_back();
					animation.FrameDuration16 = r.Unsigned<std::uint16_t>(UINT16_MAX);
					animation.Delay = r.Unsigned<std::uint16_t>(UINT16_MAX);
					animation.DelayJitter = r.Unsigned<std::uint16_t>(UINT16_MAX);
					animation.PingPongDelay = r.Unsigned<std::uint16_t>(UINT16_MAX);
					animation.IsPingPong = r.Unsigned<std::uint8_t>(UINT8_MAX);
				} else if (keyword == "frames"_s) {
					if (level.AnimatedTiles.empty()) {
						r.Fail("\"frames\" isn't after an animation");
						return false;
					}
					AnimatedTileRecord& animation = level.AnimatedTiles.back();
					while (!r.AtLineEnd() && r.IsValid()) {
						animation.Frames.push_back(r.Tile(r.Word()));
					}
				} else if (keyword == "layer"_s) {
					if (eventsStarted) {
						r.Fail("A layer is after the events");
						return false;
					}
					LayerData& layer = level.Layers.emplace_back();
					LayerRecord& h = layer.Header;
					std::memset(&h, 0, sizeof(h));
					h.Kind = LayerKind(r.Unsigned<std::uint8_t>(UINT8_MAX));
					h.Flags = r.Unsigned<std::uint16_t>(UINT16_MAX);
					h.Width = r.Unsigned<std::int32_t>(LevelFile::MaxLayerSize);
					h.Height = r.Unsigned<std::int32_t>(LevelFile::MaxLayerSize);
					h.SpeedModels = r.Unsigned<std::uint8_t>(UINT8_MAX);
					h.OffsetX = r.Float();
					h.OffsetY = r.Float();
					h.SpeedX = r.Float();
					h.SpeedY = r.Float();
					h.AutoSpeedX = r.Float();
					h.AutoSpeedY = r.Float();
					std::int64_t depth = r.Integer();
					if (depth < INT16_MIN || depth > INT16_MAX) {
						r.Fail("The depth of a layer is out of its range");
						return false;
					}
					h.Depth = std::int16_t(depth);
					h.RendererType = r.Unsigned<std::uint8_t>(UINT8_MAX);
					for (std::int32_t i = 0; i < 4; i++) {
						h.Color[i] = r.Unsigned<std::uint8_t>(UINT8_MAX);
					}
					if (!r.IsValid() || std::int64_t(h.Width) * h.Height > LevelFile::MaxLayerTiles) {
						r.Fail("A layer is too large");
						return false;
					}
					layer.Tiles.reserve(std::size_t(h.Width) * std::size_t(h.Height));
					// A layer with no width has no rows, see the writer
					const std::int32_t rowCount = (h.Width > 0 ? h.Height : 0);
					for (std::int32_t y = 0; y < rowCount && r.IsValid(); y++) {
						if (!r.NextLine()) {
							r.Fail("The tiles of a layer are cut short");
							return false;
						}
						for (std::int32_t x = 0; x < h.Width && r.IsValid(); x++) {
							layer.Tiles.push_back(r.Tile(r.Word()));
						}
						if (r.IsValid() && !r.AtLineEnd()) {
							r.Fail("A row of a layer has more tiles than the layer is wide");
						}
					}
				} else if (keyword == "event"_s) {
					const LayerData* spriteLayer = FindSpriteLayer(level);
					if (spriteLayer == nullptr) {
						r.Fail("An event is before the sprite layer");
						return false;
					}
					if (!eventsStarted) {
						// The grid is as large as the sprite layer, everything is empty but what follows
						level.Events.resize(std::size_t(spriteLayer->Header.Width) * std::size_t(spriteLayer->Header.Height), MakeEmptyEvent());
						eventsStarted = true;
					}
					std::int32_t x = r.Unsigned<std::int32_t>(spriteLayer->Header.Width - 1);
					std::int32_t y = r.Unsigned<std::int32_t>(spriteLayer->Header.Height - 1);
					if (r.IsValid()) {
						readEvent(level.Events[std::size_t(y) * std::size_t(spriteLayer->Header.Width) + std::size_t(x)]);
					}
				} else if (keyword == "offgrid"_s) {
					OffGridEventRecord& event = level.OffGridEvents.emplace_back();
					event.X = r.Unsigned<std::uint32_t>(UINT32_MAX);
					event.Y = r.Unsigned<std::uint32_t>(UINT32_MAX);
					readEvent(event.Event);
				} else if (keyword == "chunk"_s) {
					ChunkRecord& chunk = level.Chunks.emplace_back();
					chunk.Tag = r.Unsigned<std::uint32_t>(UINT32_MAX);
					r.Base64(chunk.Data);
				} else {
					r.Fail("Unknown keyword");
					return false;
				}
				// Base64 data ends by a line of its own, which was read already
				if (r.IsValid() && keyword != "chunk"_s && keyword != "data"_s && !r.AtLineEnd()) {
					r.Fail("A line has more than expected");
				}
			}
			if (!r.IsValid()) {
				return false;
			}

			const LayerData* spriteLayer = FindSpriteLayer(level);
			if (spriteLayer == nullptr) {
				r.Fail("The level has no sprite layer");
				return false;
			}
			if (!eventsStarted) {
				level.Events.resize(std::size_t(spriteLayer->Header.Width) * std::size_t(spriteLayer->Header.Height), MakeEmptyEvent());
			}
			return true;
		}

		// Name of the first section where two levels differ, for the message when the text doesn't give the level back
		const char* FindDifference(const LevelData& a, const LevelData& b)
		{
			if (a.Header.Flags != b.Header.Flags) return "flags";
			const LevelProperties& pa = a.Properties;
			const LevelProperties& pb = b.Properties;
			if (pa.DisplayName != pb.DisplayName || pa.Author != pb.Author || pa.NextLevel != pb.NextLevel || pa.SecretLevel != pb.SecretLevel ||
				pa.BonusLevel != pb.BonusLevel || pa.DefaultTileSet != pb.DefaultTileSet || pa.Music != pb.Music || pa.AmbientColor != pb.AmbientColor ||
				pa.Weather != pb.Weather || pa.WeatherIntensity != pb.WeatherIntensity || pa.WaterLevel != pb.WaterLevel || pa.CaptionTileId != pb.CaptionTileId) {
				return "properties";
			}
			if (a.HasLevelPalette != b.HasLevelPalette || a.AdditionalPalettes.size() != b.AdditionalPalettes.size()) return "palettes";
			if (a.ExtraTileSets.size() != b.ExtraTileSets.size()) return "extra tile sets";
			for (std::size_t i = 0; i < a.ExtraTileSets.size(); i++) {
				const ExtraTileSetRecord& ta = a.ExtraTileSets[i];
				const ExtraTileSetRecord& tb = b.ExtraTileSets[i];
				if (ta.Flags != tb.Flags || ta.Name != tb.Name || ta.Offset != tb.Offset || ta.Count != tb.Count || ta.Data.size() != tb.Data.size()) return "extra tile sets";
			}
			if (a.DiffuseOverrides.size() != b.DiffuseOverrides.size() || a.MaskOverrides.size() != b.MaskOverrides.size()) return "tile overrides";
			if (a.Texts.size() != b.Texts.size()) return "texts";
			for (std::size_t i = 0; i < a.Texts.size(); i++) {
				if (a.Texts[i] != b.Texts[i]) return "texts";
			}
			if (a.AnimatedTilesOffset != b.AnimatedTilesOffset || a.AnimatedTiles.size() != b.AnimatedTiles.size()) return "animated tiles";
			if (a.Layers.size() != b.Layers.size()) return "layers";
			for (std::size_t i = 0; i < a.Layers.size(); i++) {
				if (a.Layers[i].Tiles.size() != b.Layers[i].Tiles.size() || std::memcmp(&a.Layers[i].Header, &b.Layers[i].Header, offsetof(LayerRecord, SpeedModels)) != 0) return "layers";
				for (std::size_t j = 0; j < a.Layers[i].Tiles.size(); j++) {
					if (a.Layers[i].Tiles[j].TileId != b.Layers[i].Tiles[j].TileId || a.Layers[i].Tiles[j].Flags != b.Layers[i].Tiles[j].Flags) return "tiles of layers";
				}
			}
			if (a.Events.size() != b.Events.size()) return "number of events";
			for (std::size_t i = 0; i < a.Events.size(); i++) {
				const EventRecord& ea = a.Events[i];
				const EventRecord& eb = b.Events[i];
				if (ea.Type != eb.Type || (ea.Flags & ~EventRecord::NoParams) != (eb.Flags & ~EventRecord::NoParams) || ea.GeneratorFlags != eb.GeneratorFlags ||
					ea.GeneratorDelay != eb.GeneratorDelay || std::memcmp(ea.Params, eb.Params, sizeof(ea.Params)) != 0) {
					return "events";
				}
			}
			if (a.OffGridEvents.size() != b.OffGridEvents.size()) return "off-grid events";
			if (a.Chunks.size() != b.Chunks.size()) return "chunks";
			return "the way it's written";
		}

		bool ReadFile(StringView path, SmallVectorImpl<std::uint8_t>& data)
		{
			auto s = fs::Open(path, FileAccess::Read);
			if (!s->IsValid()) {
				LOGE("Cannot open \"{}\"", path);
				return false;
			}
			std::int64_t size = s->GetSize();
			data.resize_for_overwrite(std::size_t(size));
			if (s->Read(data.data(), size) != size) {
				LOGE("Cannot read \"{}\"", path);
				return false;
			}
			return true;
		}

		bool WriteFile(StringView path, const void* data, std::int64_t size)
		{
			auto s = fs::Open(path, FileAccess::Write);
			if (!s->IsValid() || (size > 0 && s->Write(data, size) != size) || !s->Flush()) {
				LOGE("Cannot write \"{}\"", path);
				return false;
			}
			return true;
		}
	}

	bool LevelText::LevelToText(StringView levelPath, StringView textPath)
	{
		SmallVector<std::uint8_t, 0> original;
		if (!ReadFile(levelPath, original)) {
			return false;
		}
		auto level = std::make_unique<LevelData>();
		if (!ReadLevel(original.data(), std::int64_t(original.size()), *level, levelPath)) {
			return false;
		}

		TextWriter w;
		WriteText(w, *level);
		ArrayView<const char> text = w.GetData();

		// What the text gives back is checked right away, against the level written as it was read
		MemoryStream expected(std::int64_t(original.size()) + 1024);
		auto parsed = std::make_unique<LevelData>();
		TextReader r(StringView(text.data(), text.size()), textPath);
		MemoryStream actual(std::int64_t(original.size()) + 1024);
		bool parsedOk = ParseText(r, *parsed);
		if (!WriteLevel(expected, *level) || !parsedOk || !WriteLevel(actual, *parsed) ||
			actual.GetSize() != expected.GetSize() || std::memcmp(actual.GetBuffer(), expected.GetBuffer(), std::size_t(actual.GetSize())) != 0) {
			LOGE("Level \"{}\" isn't the same when it's made of the text again, it differs in {}, the text isn't written", levelPath,
				parsedOk ? FindDifference(*level, *parsed) : "the text");
			return false;
		}

		if (!WriteFile(textPath, text.data(), std::int64_t(text.size()))) {
			return false;
		}
		LOGI("Level \"{}\" written as text to \"{}\"", levelPath, textPath);
		return true;
	}

	bool LevelText::TextToLevel(StringView textPath, StringView levelPath)
	{
		SmallVector<std::uint8_t, 0> text;
		if (!ReadFile(textPath, text)) {
			return false;
		}
		auto level = std::make_unique<LevelData>();
		TextReader r(StringView(reinterpret_cast<const char*>(text.data()), text.size()), textPath);
		if (!ParseText(r, *level)) {
			return false;
		}

		MemoryStream output(std::int64_t(text.size()) / 2 + 1024);
		if (!WriteLevel(output, *level)) {
			LOGE("Level cannot be made of \"{}\"", textPath);
			return false;
		}
		if (!WriteFile(levelPath, output.GetBuffer(), output.GetSize())) {
			return false;
		}
		LOGI("Level \"{}\" made of \"{}\"", levelPath, textPath);
		return true;
	}
}
