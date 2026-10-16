#include "TilePrefabs.h"

#if defined(WITH_EDITOR)

#include "RulesFile.h"

#include <string>

namespace Jazz2::Editor
{
	bool TilePrefab::Load(StringView tileSet, SmallVectorImpl<TilePrefab>& prefabs)
	{
		prefabs.clear();
		Json::Value doc;
		if (!RulesFile::Read(tileSet, doc)) {
			return false;
		}
		for (const Json::Value& item : doc["prefabs"]) {
			TilePrefab prefab;
			prefab.Name = String(item.get("name", "").asString().c_str());
			prefab.Width = item.get("width", 0).asInt();
			prefab.Height = item.get("height", 0).asInt();
			const Json::Value& tiles = item["tiles"];
			if (prefab.Width <= 0 || prefab.Height <= 0 || prefab.Width > 256 || prefab.Height > 256 || !tiles.isArray() ||
				tiles.size() != Json::ArrayIndex(prefab.Width * prefab.Height)) {
				continue;
			}
			for (const Json::Value& value : tiles) {
				Tile& tile = prefab.Tiles.emplace_back();
				if (!value.isObject()) {
					continue;
				}
				tile.Present = true;
				tile.TileSet = String(value.get("set", "").asString().c_str());
				tile.Index = std::uint16_t(value.get("i", 0).asUInt());
				tile.Flags = std::uint8_t(value.get("f", 0).asUInt());
			}
			prefabs.push_back(std::move(prefab));
		}
		return true;
	}

	bool TilePrefab::Save(StringView tileSet, const SmallVectorImpl<TilePrefab>& prefabs)
	{
		// The other rules stay as they are
		Json::Value doc;
		RulesFile::Read(tileSet, doc);
		Json::Value& list = doc["prefabs"];
		list = Json::Value(Json::arrayValue);
		for (const TilePrefab& prefab : prefabs) {
			Json::Value item(Json::objectValue);
			item["name"] = std::string(prefab.Name.data(), prefab.Name.size());
			item["width"] = prefab.Width;
			item["height"] = prefab.Height;
			Json::Value& tiles = item["tiles"];
			tiles = Json::Value(Json::arrayValue);
			for (const Tile& tile : prefab.Tiles) {
				if (!tile.Present) {
					tiles.append(Json::Value(Json::nullValue));
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
				tiles.append(value);
			}
			list.append(item);
		}
		return RulesFile::Write(tileSet, doc);
	}

	TilePrefab TilePrefab::FromStamp(const LevelDocument& document, const TileStamp& stamp, StringView name)
	{
		TilePrefab prefab;
		prefab.Name = name;
		prefab.Width = stamp.Width;
		prefab.Height = stamp.Height;
		for (std::size_t i = 0; i < stamp.Tiles.size(); i++) {
			Tile& tile = prefab.Tiles.emplace_back();
			const TileRef& source = stamp.Tiles[i];
			if (!stamp.Present[i] || source.Source == TileRef::Animated || source.Source == TileRef::Raw) {
				continue;
			}
			if (source.Source != TileRef::DefaultTileSet) {
				if (std::size_t(source.Source) - 1 >= document.ExtraTileSets.size()) {
					continue;
				}
				// By the index in the file of the tile set, so another level that takes another part of it finds it too
				const Formats::ExtraTileSetRecord& extra = document.ExtraTileSets[source.Source - 1];
				tile.TileSet = extra.Name;
				tile.Index = std::uint16_t(source.Index + extra.Offset);
			} else {
				tile.Index = source.Index;
			}
			tile.Present = true;
			tile.Flags = source.Flags;
		}
		return prefab;
	}

	TileStamp TilePrefab::ToStamp(const LevelDocument& document) const
	{
		TileStamp stamp;
		stamp.Width = Width;
		stamp.Height = Height;
		for (const Tile& tile : Tiles) {
			TileRef ref{};
			bool present = tile.Present;
			if (present && !tile.TileSet.empty()) {
				present = false;
				for (std::size_t i = 0; i < document.ExtraTileSets.size() && i < TileRef::LastExtraTileSet; i++) {
					const Formats::ExtraTileSetRecord& extra = document.ExtraTileSets[i];
					if (extra.Name == tile.TileSet && tile.Index >= extra.Offset && tile.Index < extra.Offset + extra.Count) {
						ref = TileRef{ std::uint16_t(tile.Index - extra.Offset), std::uint8_t(i + 1), tile.Flags };
						present = true;
						break;
					}
				}
			} else if (present) {
				ref = TileRef{ tile.Index, TileRef::DefaultTileSet, tile.Flags };
			}
			stamp.Tiles.push_back(ref);
			stamp.Present.push_back(present);
		}
		return stamp;
	}
}

#endif
