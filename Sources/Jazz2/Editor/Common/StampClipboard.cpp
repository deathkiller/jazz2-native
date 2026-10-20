#include "StampClipboard.h"

#if defined(WITH_EDITOR)

#include "../../EventType.h"

#include <Containers/StringConcatenable.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace Death::Containers::Literals;

namespace Jazz2::Editor::StampClipboard
{
	namespace
	{
		// Prefix of a stamp in the clipboard of the system
		constexpr StringView Prefix = "{\"J2R\":"_s;
		// The clipboard of the system can hold anything, a stamp is read only if it's whole and of a sane size
		constexpr std::int64_t MaxSide = MaxStampSide;
		constexpr std::int64_t MaxTiles = MaxStampTiles;
		constexpr std::int64_t MaxLayeredTiles = 1 << 22;
		constexpr std::int32_t MaxLayers = 255;
		constexpr std::size_t MaxEvents = 1 << 16;
		// Events as numbers, each an offset, the type, the flags, the generator and the parameters
		constexpr std::int32_t ValuesPerEvent = 6 + Formats::LevelFile::EventParamsSize;

		String ToText(std::int64_t value)
		{
			return String(std::to_string(value).c_str());
		}

		// Reads a whole number, `false` if there's none or it's out of the range, a comma after it is skipped
		bool ReadNumber(const char*& p, std::int64_t min, std::int64_t max, std::int64_t& value)
		{
			char* end;
			long long number = std::strtoll(p, &end, 10);
			if (end == p || number < min || number > max) {
				return false;
			}
			value = std::int64_t(number);
			p = (*end == ',' ? end + 1 : end);
			return true;
		}

		// Skips a text, with spaces before it, `false` if it isn't there
		bool Skip(const char*& p, const char* text)
		{
			while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
				p++;
			}
			std::size_t length = std::strlen(text);
			if (std::strncmp(p, text, length) != 0) {
				return false;
			}
			p += length;
			return true;
		}

		// The number after a key anywhere in the text
		bool FindNumber(StringView text, StringView key, std::int64_t min, std::int64_t max, std::int64_t& value)
		{
			StringView found = text.find(key);
			if (found.empty()) {
				return false;
			}
			String rest = text.suffix(found.end());
			const char* p = rest.data();
			return ReadNumber(p, min, max, value);
		}

		void AppendTiles(String& text, const TileStamp& stamp)
		{
			for (std::size_t i = 0; i < stamp.Tiles.size(); i++) {
				const TileRef& tile = stamp.Tiles[i];
				bool present = (i < stamp.Present.size() && stamp.Present[i]);
				char buffer[48];
				std::int32_t length = std::snprintf(buffer, sizeof(buffer), "%s%d,%d,%d", i > 0 ? "," : "",
					present ? std::int32_t(tile.Source) : -1, std::int32_t(tile.Index), std::int32_t(tile.Flags));
				text += StringView(buffer, std::size_t(length));
			}
		}

		// Reads tiles of a stamp of a given size, a stamp with fewer tiles than its size isn't a stamp
		bool ReadTileValues(const char*& p, std::int32_t width, std::int32_t height, TileStamp& result)
		{
			result = TileStamp();
			result.Width = width;
			result.Height = height;
			std::size_t count = std::size_t(width) * std::size_t(height);
			result.Tiles.reserve(count);
			result.Present.reserve(count);
			for (std::size_t i = 0; i < count; i++) {
				std::int64_t source, index, flags;
				if (!ReadNumber(p, -1, UINT8_MAX, source) || !ReadNumber(p, 0, UINT16_MAX, index) || !ReadNumber(p, 0, UINT8_MAX, flags)) {
					return false;
				}
				result.Tiles.push_back(TileRef{ std::uint16_t(index), std::uint8_t(source < 0 ? 0 : source), std::uint8_t(flags) });
				result.Present.push_back(source >= 0);
			}
			return true;
		}

		void AppendEvent(String& text, bool first, Vector2i offset, const Formats::EventRecord& event)
		{
			char buffer[256];
			std::int32_t length = std::snprintf(buffer, sizeof(buffer), "%s%d,%d,%u,%u,%u,%u", first ? "" : ",",
				offset.X, offset.Y, std::uint32_t(event.Type), std::uint32_t(event.Flags),
				std::uint32_t(event.GeneratorFlags), std::uint32_t(event.GeneratorDelay));
			for (std::int32_t j = 0; j < Formats::LevelFile::EventParamsSize; j++) {
				length += std::snprintf(buffer + length, sizeof(buffer) - length, ",%u", std::uint32_t(event.Params[j]));
			}
			text += StringView(buffer, std::size_t(length));
		}

		// The events on the grid as `"events":[...]`, those off it as `"offgrid":[...]` after them, older versions read only the first
		void AppendEvents(String& text, const EventStamp& stamp)
		{
			text += "\"events\":["_s;
			for (std::size_t i = 0; i < stamp.Items.size(); i++) {
				AppendEvent(text, i == 0, stamp.Items[i].Offset, stamp.Items[i].Event);
			}
			text += "]"_s;
			if (!stamp.OffGrid.empty()) {
				text += ",\"offgrid\":["_s;
				for (std::size_t i = 0; i < stamp.OffGrid.size(); i++) {
					AppendEvent(text, i == 0, stamp.OffGrid[i].Offset, stamp.OffGrid[i].Event);
				}
				text += "]"_s;
			}
		}

		// Reads a list of events as numbers, `false` if it isn't whole
		template<class TCallback>
		bool ReadEventList(const char* p, std::int64_t maxOffset, TCallback&& callback)
		{
			std::size_t count = 0;
			while (*p != '\0' && *p != ']') {
				std::int64_t numbers[ValuesPerEvent];
				for (std::int32_t i = 0; i < ValuesPerEvent; i++) {
					// Offsets within the largest event grid, everything else a byte, but the type
					std::int64_t min = (i < 2 ? -maxOffset : 0);
					std::int64_t max = (i < 2 ? maxOffset : (i == 2 ? UINT16_MAX : UINT8_MAX));
					if (!ReadNumber(p, min, max, numbers[i])) {
						return false;
					}
				}
				if (count >= MaxEvents) {
					return false;
				}
				count++;
				// An event this version doesn't know is left out, it couldn't be placed nor shown
				if (numbers[2] >= std::int64_t(EventType::Count)) {
					continue;
				}
				Formats::EventRecord event{};
				event.Type = std::uint16_t(numbers[2]);
				event.Flags = std::uint8_t(numbers[3]);
				event.GeneratorFlags = std::uint8_t(numbers[4]);
				event.GeneratorDelay = std::uint8_t(numbers[5]);
				for (std::int32_t j = 0; j < Formats::LevelFile::EventParamsSize; j++) {
					event.Params[j] = std::uint8_t(numbers[6 + j]);
				}
				callback(Vector2i(std::int32_t(numbers[0]), std::int32_t(numbers[1])), event);
			}
			return true;
		}

		// A source of tiles of the level a stamp was copied from, see AppendSources()
		struct SourceInfo
		{
			String Name;
			std::int32_t Offset;
			bool Embedded;
		};

		bool EqualsIgnoreCase(StringView a, StringView b)
		{
			if (a.size() != b.size()) {
				return false;
			}
			for (std::size_t i = 0; i < a.size(); i++) {
				char ca = a[i], cb = b[i];
				if (ca >= 'A' && ca <= 'Z') ca = char(ca - 'A' + 'a');
				if (cb >= 'A' && cb <= 'Z') cb = char(cb - 'A' + 'a');
				if (ca != cb) {
					return false;
				}
			}
			return true;
		}

		// The tile sets of the level as `"sets":[name,offset,embedded,...]`, the default one first, then the extra ones by
		// their sources, so another level with them in other slots gets the same tiles
		void AppendSources(String& text, const LevelDocument& document)
		{
			text += ",\"sets\":["_s;
			auto append = [&text](StringView name, std::int32_t offset, bool embedded, bool first) {
				String quoted = (first ? "\""_s : ",\""_s);
				for (char c : name) {
					if (c == '"' || c == '\\') {
						quoted += "\\"_s;
					} else if (std::uint8_t(c) < 0x20) {
						continue;
					}
					quoted += StringView(&c, 1);
				}
				quoted += "\","_s + ToText(offset) + (embedded ? ",1"_s : ",0"_s);
				text += quoted;
			};
			append(document.Properties.DefaultTileSet, 0, false, true);
			for (std::size_t i = 0; i < document.ExtraTileSets.size() && i < TileRef::LastExtraTileSet; i++) {
				const Formats::ExtraTileSetRecord& extra = document.ExtraTileSets[i];
				append(extra.Name, std::int32_t(extra.Offset), (extra.Flags & Formats::ExtraTileSetRecord::Embedded) != 0, false);
			}
			text += "]"_s;
		}

		// Reads the tile sets written by AppendSources(), `false` if there are none, as from an older version
		bool ReadSources(StringView text, SmallVectorImpl<SourceInfo>& sources)
		{
			sources.clear();
			StringView found = text.find("\"sets\":["_s);
			if (found.empty()) {
				return false;
			}
			String values = text.suffix(found.end());
			const char* p = values.data();
			while (!Skip(p, "]")) {
				if (sources.size() > TileRef::LastExtraTileSet || !Skip(p, "\"")) {
					sources.clear();
					return false;
				}
				SourceInfo& source = sources.emplace_back();
				while (*p != '"') {
					if (*p == '\\' && p[1] != '\0') {
						p++;
					} else if (*p == '\0') {
						sources.clear();
						return false;
					}
					source.Name += StringView(p, 1);
					p++;
				}
				p++;
				std::int64_t offset, embedded;
				if (!Skip(p, ",") || !ReadNumber(p, 0, UINT16_MAX, offset) || !ReadNumber(p, 0, 1, embedded)) {
					sources.clear();
					return false;
				}
				source.Offset = std::int32_t(offset);
				source.Embedded = (embedded != 0);
			}
			return !sources.empty();
		}

		// Moves the tiles of the tile sets of the level they were copied from to the sources of a level that has the same
		// tile sets, by the name and the index in the file, tiles of a tile set it hasn't are left out
		void RemapSources(const LevelDocument& document, const SmallVectorImpl<SourceInfo>& sources, TileStamp& stamp)
		{
			std::size_t extraCount = std::min<std::size_t>(document.ExtraTileSets.size(), TileRef::LastExtraTileSet);
			// Copied from the same level, or one with the same tile sets in the same slots, nothing changes
			bool same = (sources.size() == extraCount + 1 && EqualsIgnoreCase(sources[0].Name, document.Properties.DefaultTileSet));
			for (std::size_t i = 0; same && i < extraCount; i++) {
				const Formats::ExtraTileSetRecord& extra = document.ExtraTileSets[i];
				same = (EqualsIgnoreCase(sources[i + 1].Name, extra.Name) && sources[i + 1].Offset == std::int32_t(extra.Offset) &&
					sources[i + 1].Embedded == ((extra.Flags & Formats::ExtraTileSetRecord::Embedded) != 0));
			}
			if (same) {
				return;
			}
			for (std::size_t i = 0; i < stamp.Tiles.size() && i < stamp.Present.size(); i++) {
				TileRef& tile = stamp.Tiles[i];
				if (!stamp.Present[i] || tile.IsEmpty() || tile.Source == TileRef::Raw || tile.Source == TileRef::Animated) {
					continue;
				}
				bool found = false;
				if (tile.Source < sources.size() && !sources[tile.Source].Embedded) {
					// A tile set the level has in itself is of that level only
					const SourceInfo& source = sources[tile.Source];
					std::int32_t fileIndex = source.Offset + tile.Index;
					if (EqualsIgnoreCase(source.Name, document.Properties.DefaultTileSet)) {
						tile.Index = std::uint16_t(fileIndex);
						tile.Source = TileRef::DefaultTileSet;
						found = (fileIndex <= UINT16_MAX);
					}
					for (std::size_t j = 0; !found && j < extraCount; j++) {
						const Formats::ExtraTileSetRecord& extra = document.ExtraTileSets[j];
						if ((extra.Flags & Formats::ExtraTileSetRecord::Embedded) == 0 && EqualsIgnoreCase(source.Name, extra.Name) &&
							fileIndex >= std::int32_t(extra.Offset) && fileIndex < std::int32_t(extra.Offset + extra.Count)) {
							tile.Index = std::uint16_t(fileIndex - std::int32_t(extra.Offset));
							tile.Source = std::uint8_t(j + 1);
							found = true;
						}
					}
				}
				if (!found) {
					tile = TileRef{};
					stamp.Present[i] = false;
				}
			}
		}
	}

	String WriteTiles(const TileStamp& stamp, const LevelDocument* document)
	{
		String text = Prefix + "1,\"w\":"_s + ToText(stamp.Width) + ",\"h\":"_s + ToText(stamp.Height) + ",\"t\":["_s;
		AppendTiles(text, stamp);
		text += "]"_s;
		if (document != nullptr) {
			AppendSources(text, *document);
		}
		text += "}"_s;
		return text;
	}

	bool ReadTiles(StringView text, TileStamp& stamp, const LevelDocument* document)
	{
		if (!text.hasPrefix(Prefix)) {
			return false;
		}
		std::int64_t width, height;
		if (!FindNumber(text, "\"w\":"_s, 1, MaxSide, width) || !FindNumber(text, "\"h\":"_s, 1, MaxSide, height) || width * height > MaxTiles) {
			return false;
		}
		StringView found = text.find("\"t\":["_s);
		if (found.empty()) {
			return false;
		}
		String values = text.suffix(found.end());
		const char* p = values.data();
		TileStamp result;
		if (!ReadTileValues(p, std::int32_t(width), std::int32_t(height), result)) {
			return false;
		}
		SmallVector<SourceInfo, 0> sources;
		if (document != nullptr && ReadSources(text, sources)) {
			RemapSources(*document, sources, result);
		}
		stamp = Death::move(result);
		return true;
	}

	String WriteEvents(const EventStamp& stamp)
	{
		String text = Prefix + "1,"_s;
		AppendEvents(text, stamp);
		text += "}"_s;
		return text;
	}

	bool ReadEvents(StringView text, EventStamp& stamp)
	{
		if (!text.hasPrefix(Prefix)) {
			return false;
		}
		StringView found = text.find("\"events\":["_s);
		if (found.empty()) {
			return false;
		}
		EventStamp result;
		String values = text.suffix(found.end());
		if (!ReadEventList(values.data(), MaxSide, [&result](Vector2i offset, const Formats::EventRecord& event) {
			result.Items.push_back(EventStamp::Item{ offset, event });
		})) {
			return false;
		}
		// Off the grid by pixels, so within the largest level in pixels
		StringView foundOffGrid = text.find("\"offgrid\":["_s);
		if (!foundOffGrid.empty()) {
			String offGridValues = text.suffix(foundOffGrid.end());
			if (!ReadEventList(offGridValues.data(), MaxSide * 32, [&result](Vector2i offset, const Formats::EventRecord& event) {
				result.OffGrid.push_back(EventStamp::OffGridItem{ offset, event });
			})) {
				return false;
			}
		}
		if (result.IsEmpty()) {
			return false;
		}
		stamp = Death::move(result);
		return true;
	}

	String WriteLayered(const LayeredStamp& stamp, const LevelDocument* document)
	{
		String text = Prefix + "2,\"w\":"_s + ToText(stamp.Width) + ",\"h\":"_s + ToText(stamp.Height) + ",\"x\":"_s + ToText(stamp.Origin.X) +
			",\"y\":"_s + ToText(stamp.Origin.Y) + ",\"layers\":["_s;
		for (std::size_t i = 0; i < stamp.Layers.size(); i++) {
			const LayeredStamp::Layer& layer = stamp.Layers[i];
			text += (i > 0 ? ",{\"o\":"_s : "{\"o\":"_s) + ToText(layer.FromSpriteLayer) + ",\"t\":["_s;
			AppendTiles(text, layer.Tiles);
			text += "]}"_s;
		}
		text += "],"_s;
		AppendEvents(text, stamp.Events);
		if (document != nullptr) {
			AppendSources(text, *document);
		}
		text += "}"_s;
		return text;
	}

	bool ReadLayered(StringView text, LayeredStamp& stamp, const LevelDocument* document)
	{
		if (!text.hasPrefix("{\"J2R\":2,"_s)) {
			return false;
		}
		std::int64_t width, height, x = 0, y = 0;
		if (!FindNumber(text, "\"w\":"_s, 1, MaxSide, width) || !FindNumber(text, "\"h\":"_s, 1, MaxSide, height) || width * height > MaxTiles) {
			return false;
		}
		FindNumber(text, "\"x\":"_s, -MaxSide, MaxSide, x);
		FindNumber(text, "\"y\":"_s, -MaxSide, MaxSide, y);

		LayeredStamp result;
		result.Width = std::int32_t(width);
		result.Height = std::int32_t(height);
		result.Origin = Vector2i(std::int32_t(x), std::int32_t(y));
		StringView found = text.find("\"layers\":["_s);
		if (found.empty()) {
			return false;
		}
		String values = text.suffix(found.end());
		const char* p = values.data();
		std::int64_t tiles = 0;
		while (!Skip(p, "]")) {
			std::int64_t fromSprite;
			if (std::int32_t(result.Layers.size()) >= MaxLayers || !Skip(p, "{\"o\":") || !ReadNumber(p, -MaxLayers, MaxLayers, fromSprite) ||
				!Skip(p, "\"t\":[")) {
				return false;
			}
			tiles += width * height;
			if (tiles > MaxLayeredTiles) {
				return false;
			}
			LayeredStamp::Layer& layer = result.Layers.emplace_back();
			layer.FromSpriteLayer = std::int32_t(fromSprite);
			if (!ReadTileValues(p, result.Width, result.Height, layer.Tiles) || !Skip(p, "]}")) {
				return false;
			}
			Skip(p, ",");
		}
		// The events are optional, a copy without any has an empty list
		ReadEvents(text, result.Events);
		if (result.IsEmpty()) {
			return false;
		}
		SmallVector<SourceInfo, 0> sources;
		if (document != nullptr && ReadSources(text, sources)) {
			for (LayeredStamp::Layer& layer : result.Layers) {
				RemapSources(*document, sources, layer.Tiles);
			}
		}
		stamp = Death::move(result);
		return true;
	}

	bool HasTile(const LevelDocument& document, const TileRef& tile)
	{
		if (tile.IsEmpty()) {
			return true;
		}
		if (tile.Source == TileRef::Raw) {
			return (tile.Index < document.GetAnimatedTilesOffset());
		}
		std::int32_t count = document.GetSourceTileCount(tile.Source);
		if (tile.Source == TileRef::DefaultTileSet && count < 0) {
			// The tile set wasn't found, its tiles can't be told apart from anything else
			return (tile.Index < document.GetAnimatedTilesOffset());
		}
		return (tile.Index < count);
	}

	void RemoveUnknownTiles(const LevelDocument& document, TileStamp& stamp)
	{
		for (std::size_t i = 0; i < stamp.Tiles.size() && i < stamp.Present.size(); i++) {
			if (stamp.Present[i] && !HasTile(document, stamp.Tiles[i])) {
				stamp.Tiles[i] = TileRef{};
				stamp.Present[i] = false;
			}
		}
	}
}

#endif
