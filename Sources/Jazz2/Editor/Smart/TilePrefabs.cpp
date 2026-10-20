#include "TilePrefabs.h"

#if defined(WITH_EDITOR)

#include "RulesFile.h"
#include "../Common/TileSource.h"
#include "../Files/JsonAccess.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace Jazz2::Editor
{
	namespace
	{
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
	}

	bool TilePrefab::Load(StringView tileSet, SmallVectorImpl<TilePrefab>& prefabs)
	{
		prefabs.clear();
		Json::Value doc;
		if (!RulesFile::Read(tileSet, doc)) {
			return false;
		}
		// The file can be written by hand or come with a tile set, a value of another type than expected is left out
		const Json::Value& list = JsonAccess::Member(doc, "prefabs");
		if (!list.isArray()) {
			return true;
		}
		for (const Json::Value& item : list) {
			if (!item.isObject()) {
				continue;
			}
			TilePrefab prefab;
			prefab.Name = JsonAccess::GetString(item, "name");
			prefab.Width = JsonAccess::GetInt(item, "width", 0);
			prefab.Height = JsonAccess::GetInt(item, "height", 0);
			if (prefab.Width <= 0 || prefab.Height <= 0 || prefab.Width > 256 || prefab.Height > 256 ||
				!ReadTiles(JsonAccess::Member(item, "tiles"), prefab.Width * prefab.Height, prefab.Tiles)) {
				continue;
			}
			// Tiles of other layers and events, by newer versions
			const Json::Value& layers = JsonAccess::Member(item, "layers");
			if (layers.isArray()) {
				for (const Json::Value& value : layers) {
					LayerTiles layer;
					layer.FromLayer = std::clamp<std::int32_t>(JsonAccess::GetInt(value, "o", 0), -255, 255);
					if (layer.FromLayer != 0 && ReadTiles(JsonAccess::Member(value, "tiles"), prefab.Width * prefab.Height, layer.Tiles)) {
						prefab.ExtraLayers.push_back(Death::move(layer));
					}
				}
			}
			ReadEvents(JsonAccess::Member(item, "events"), Vector2i(prefab.Width, prefab.Height), prefab.Events);
			// Those off the grid by pixels, by even newer versions
			ReadEvents(JsonAccess::Member(item, "offgrid"), Vector2i(prefab.Width * 32, prefab.Height * 32), prefab.OffGridEvents);
			prefabs.push_back(Death::move(prefab));
		}
		return true;
	}

	bool TilePrefab::Save(StringView tileSet, const SmallVectorImpl<TilePrefab>& prefabs)
	{
		// The other rules stay as they are, rules that can't be read aren't written over, all would be lost
		Json::Value doc;
		if (RulesFile::TryRead(tileSet, doc) == RulesFile::ReadResult::Unreadable) {
			LOGW("Prefabs of tile set \"{}\" aren't saved, the rules there can't be read, fix or remove the file first", tileSet);
			return false;
		}
		Json::Value& list = doc["prefabs"];
		list = Json::Value(Json::arrayValue);
		for (const TilePrefab& prefab : prefabs) {
			Json::Value item(Json::objectValue);
			item["name"] = std::string(prefab.Name.data(), prefab.Name.size());
			item["width"] = prefab.Width;
			item["height"] = prefab.Height;
			item["tiles"] = WriteTiles(prefab.Tiles);
			if (!prefab.ExtraLayers.empty()) {
				Json::Value layers(Json::arrayValue);
				for (const LayerTiles& layer : prefab.ExtraLayers) {
					Json::Value value(Json::objectValue);
					value["o"] = layer.FromLayer;
					value["tiles"] = WriteTiles(layer.Tiles);
					layers.append(value);
				}
				item["layers"] = layers;
			}
			if (!prefab.Events.empty()) {
				item["events"] = WriteEvents(prefab.Events);
			}
			if (!prefab.OffGridEvents.empty()) {
				item["offgrid"] = WriteEvents(prefab.OffGridEvents);
			}
			list.append(item);
		}
		return RulesFile::Write(tileSet, doc);
	}

	void TilePrefab::ReadEvents(const Json::Value& events, Vector2i limit, SmallVectorImpl<PrefabEvent>& result)
	{
		if (!events.isArray()) {
			return;
		}
		for (const Json::Value& value : events) {
			std::int32_t x = JsonAccess::GetInt(value, "x", -1), y = JsonAccess::GetInt(value, "y", -1);
			std::uint32_t type = JsonAccess::GetUInt(value, "type", 0);
			if (x < 0 || y < 0 || x >= limit.X || y >= limit.Y || type == 0 || type > UINT16_MAX) {
				continue;
			}
			PrefabEvent& event = result.emplace_back();
			event.Offset = Vector2i(x, y);
			std::memset(&event.Event, 0, sizeof(event.Event));
			event.Event.Type = std::uint16_t(type);
			event.Event.Flags = std::uint8_t(JsonAccess::GetUInt(value, "flags", 0) & 0xFF);
			event.Event.GeneratorFlags = std::uint8_t(JsonAccess::GetUInt(value, "gen", 0) & 0xFF);
			event.Event.GeneratorDelay = std::uint8_t(JsonAccess::GetUInt(value, "delay", 0) & 0xFF);
			const Json::Value& params = JsonAccess::Member(value, "p");
			if (params.isArray()) {
				for (Json::ArrayIndex j = 0; j < params.size() && j < Json::ArrayIndex(Formats::LevelFile::EventParamsSize); j++) {
					event.Event.Params[j] = std::uint8_t(JsonAccess::ToUInt(params[j], 0) & 0xFF);
				}
			}
		}
	}

	Json::Value TilePrefab::WriteEvents(const SmallVectorImpl<PrefabEvent>& events)
	{
		Json::Value list(Json::arrayValue);
		for (const PrefabEvent& event : events) {
			Json::Value value(Json::objectValue);
			value["x"] = event.Offset.X;
			value["y"] = event.Offset.Y;
			value["type"] = std::uint32_t(event.Event.Type);
			value["flags"] = std::uint32_t(event.Event.Flags);
			value["gen"] = std::uint32_t(event.Event.GeneratorFlags);
			value["delay"] = std::uint32_t(event.Event.GeneratorDelay);
			Json::Value params(Json::arrayValue);
			for (std::int32_t j = 0; j < Formats::LevelFile::EventParamsSize; j++) {
				params.append(std::uint32_t(event.Event.Params[j]));
			}
			value["p"] = params;
			list.append(value);
		}
		return list;
	}

	bool TilePrefab::ReadTiles(const Json::Value& tiles, std::int32_t count, SmallVectorImpl<Tile>& result)
	{
		if (!tiles.isArray() || tiles.size() != Json::ArrayIndex(count)) {
			return false;
		}
		for (const Json::Value& value : tiles) {
			Tile& tile = result.emplace_back();
			std::uint32_t index = JsonAccess::GetUInt(value, "i", 0);
			if (!value.isObject() || index > UINT16_MAX) {
				continue;
			}
			tile.Present = true;
			tile.TileSet = JsonAccess::GetString(value, "set");
			tile.Index = std::uint16_t(index);
			tile.Flags = std::uint8_t(JsonAccess::GetUInt(value, "f", 0) & 0xFF);
		}
		return true;
	}

	Json::Value TilePrefab::WriteTiles(const SmallVectorImpl<Tile>& tiles)
	{
		Json::Value list(Json::arrayValue);
		for (const Tile& tile : tiles) {
			if (!tile.Present) {
				list.append(Json::Value(Json::nullValue));
				continue;
			}
			Json::Value value(Json::objectValue);
			if (!tile.TileSet.empty()) {
				value["set"] = std::string(tile.TileSet.data(), tile.TileSet.size());
			}
			value["i"] = tile.Index;
			if (tile.Flags != 0) {
				value["f"] = tile.Flags;
			}
			list.append(value);
		}
		return list;
	}

	std::int32_t TilePrefab::TilesFromStamp(const LevelDocument& document, const TileStamp& stamp, SmallVectorImpl<Tile>& result)
	{
		std::int32_t dropped = 0;
		for (std::size_t i = 0; i < stamp.Tiles.size(); i++) {
			Tile& tile = result.emplace_back();
			const TileRef& source = stamp.Tiles[i];
			if (i >= stamp.Present.size() || !stamp.Present[i] || source.Source == TileRef::Animated || source.Source == TileRef::Raw) {
				continue;
			}
			// By the index in the file of the tile set, so another level that takes another part of it finds it too
			TileSource resolved;
			if (!ResolveTileSource(document, source.Source, resolved)) {
				continue;
			}
			if (resolved.IsEmbedded()) {
				// A tile set the level has in itself is of this level only, another level would take its own tiles instead
				dropped++;
				continue;
			}
			if (resolved.Extra != nullptr) {
				tile.TileSet = resolved.Name;
			}
			tile.Index = std::uint16_t(source.Index + resolved.Offset);
			tile.Present = true;
			tile.Flags = source.Flags;
		}
		return dropped;
	}

	TileStamp TilePrefab::TilesToStamp(const LevelDocument& document, const SmallVectorImpl<Tile>& tiles, std::int32_t width, std::int32_t height)
	{
		TileStamp stamp;
		stamp.Width = width;
		stamp.Height = height;
		for (const Tile& tile : tiles) {
			TileRef ref{};
			bool present = tile.Present;
			if (present && !tile.TileSet.empty()) {
				// By the name in any case, as the rules find tile sets, never one the level has in itself
				present = false;
				for (std::size_t i = 0; i < document.ExtraTileSets.size() && i < TileRef::LastExtraTileSet; i++) {
					const Formats::ExtraTileSetRecord& extra = document.ExtraTileSets[i];
					if ((extra.Flags & Formats::ExtraTileSetRecord::Embedded) == 0 && EqualsIgnoreCase(extra.Name, tile.TileSet) &&
						tile.Index >= extra.Offset && tile.Index < extra.Offset + extra.Count) {
						ref = TileRef{ std::uint16_t(tile.Index - extra.Offset), std::uint8_t(i + 1), tile.Flags };
						present = true;
						break;
					}
				}
			} else if (present) {
				// A tile past the end of the tile set, imported again with fewer tiles, would be saved as another tile
				ref = TileRef{ tile.Index, TileRef::DefaultTileSet, tile.Flags };
				if (!StampClipboard::HasTile(document, ref)) {
					ref = TileRef{};
					present = false;
				}
			}
			stamp.Tiles.push_back(ref);
			stamp.Present.push_back(present);
		}
		return stamp;
	}

	TilePrefab TilePrefab::FromStamp(const LevelDocument& document, const TileStamp& stamp, StringView name)
	{
		TilePrefab prefab;
		prefab.Name = name;
		prefab.Width = stamp.Width;
		prefab.Height = stamp.Height;
		prefab.DroppedTiles = TilesFromStamp(document, stamp, prefab.Tiles);
		return prefab;
	}

	TileStamp TilePrefab::ToStamp(const LevelDocument& document) const
	{
		return TilesToStamp(document, Tiles, Width, Height);
	}

	TilePrefab TilePrefab::FromLayered(const LevelDocument& document, const LayeredStamp& stamp, std::int32_t mainLayer, StringView name)
	{
		TilePrefab prefab;
		prefab.Name = name;
		prefab.Width = stamp.Width;
		prefab.Height = stamp.Height;
		bool hasMain = false;
		for (const LayeredStamp::Layer& layer : stamp.Layers) {
			if (layer.FromSpriteLayer == mainLayer && !hasMain) {
				prefab.DroppedTiles += TilesFromStamp(document, layer.Tiles, prefab.Tiles);
				hasMain = true;
			} else if (layer.FromSpriteLayer != mainLayer) {
				LayerTiles& extra = prefab.ExtraLayers.emplace_back();
				extra.FromLayer = layer.FromSpriteLayer - mainLayer;
				prefab.DroppedTiles += TilesFromStamp(document, layer.Tiles, extra.Tiles);
			}
		}
		if (!hasMain) {
			// The layer it's used on gets nothing of its own
			prefab.Tiles.resize(std::size_t(stamp.Width) * std::size_t(stamp.Height));
		}
		for (const EventStamp::Item& item : stamp.Events.Items) {
			if (item.Offset.X >= 0 && item.Offset.Y >= 0 && item.Offset.X < stamp.Width && item.Offset.Y < stamp.Height) {
				prefab.Events.push_back(PrefabEvent{ item.Offset, item.Event });
			}
		}
		for (const EventStamp::OffGridItem& item : stamp.Events.OffGrid) {
			if (item.Offset.X >= 0 && item.Offset.Y >= 0 && item.Offset.X < stamp.Width * 32 && item.Offset.Y < stamp.Height * 32) {
				prefab.OffGridEvents.push_back(PrefabEvent{ item.Offset, item.Event });
			}
		}
		return prefab;
	}

	LayeredStamp TilePrefab::ToLayered(const LevelDocument& document, std::int32_t mainLayer) const
	{
		LayeredStamp stamp;
		stamp.Width = Width;
		stamp.Height = Height;
		LayeredStamp::Layer& main = stamp.Layers.emplace_back();
		main.FromSpriteLayer = mainLayer;
		main.Tiles = TilesToStamp(document, Tiles, Width, Height);
		for (const LayerTiles& extra : ExtraLayers) {
			LayeredStamp::Layer& layer = stamp.Layers.emplace_back();
			layer.FromSpriteLayer = mainLayer + extra.FromLayer;
			layer.Tiles = TilesToStamp(document, extra.Tiles, Width, Height);
		}
		for (const PrefabEvent& event : Events) {
			EventStamp::Item& item = stamp.Events.Items.emplace_back();
			item.Offset = event.Offset;
			item.Event = event.Event;
		}
		// Linked identifiers of both kinds are remapped when it's pasted, see RemapLinkedIds()
		for (const PrefabEvent& event : OffGridEvents) {
			EventStamp::OffGridItem& item = stamp.Events.OffGrid.emplace_back();
			item.Offset = event.Offset;
			item.Event = event.Event;
		}
		return stamp;
	}
}

#endif
