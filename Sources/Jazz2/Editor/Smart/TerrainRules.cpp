#include "TerrainRules.h"

#if defined(WITH_EDITOR)

#include "RulesFile.h"
#include "SmartTileModel.h"

#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>

#include <algorithm>
#include <string>

using namespace Death::Containers;
using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	std::uint8_t TerrainRules::Reduce(std::uint8_t neighbors)
	{
		using N = SmartTileModel::Neighbor;
		std::uint8_t result = neighbors & std::uint8_t(N::Up | N::Left | N::Right | N::Down);
		if ((neighbors & N::UpLeft) != 0 && (neighbors & N::Up) != 0 && (neighbors & N::Left) != 0) {
			result |= N::UpLeft;
		}
		if ((neighbors & N::UpRight) != 0 && (neighbors & N::Up) != 0 && (neighbors & N::Right) != 0) {
			result |= N::UpRight;
		}
		if ((neighbors & N::DownLeft) != 0 && (neighbors & N::Down) != 0 && (neighbors & N::Left) != 0) {
			result |= N::DownLeft;
		}
		if ((neighbors & N::DownRight) != 0 && (neighbors & N::Down) != 0 && (neighbors & N::Right) != 0) {
			result |= N::DownRight;
		}
		return result;
	}

	void TerrainRules::GetPatterns(SmallVectorImpl<std::uint8_t>& patterns)
	{
		patterns.clear();
		for (std::int32_t i = 0; i < 256; i++) {
			if (Reduce(std::uint8_t(i)) == i) {
				patterns.push_back(std::uint8_t(i));
			}
		}
		// The most solid first, as ground is mostly
		std::stable_sort(patterns.begin(), patterns.end(), [](std::uint8_t a, std::uint8_t b) {
			auto bits = [](std::uint8_t v) {
				std::int32_t count = 0;
				for (; v != 0; v &= std::uint8_t(v - 1)) {
					count++;
				}
				return count;
			};
			return bits(a) > bits(b);
		});
	}

	TerrainRules::TerrainRules()
		: _current(0), _revision(0), _document(nullptr)
	{
		Clear();
		_terrains.clear();
		AddTerrain("Ground"_s);
	}

	bool TerrainRules::IsRuleTile(const LevelDocument& document, const TileRef& tile)
	{
		if (tile.IsEmpty() || tile.IsAnimated() || tile.Source == TileRef::Raw || (tile.Flags & Formats::TileRecord::ModifierMask) != 0) {
			return false;
		}
		if (tile.Source == TileRef::DefaultTileSet) {
			return true;
		}
		// A tile set the level has in itself is of this level only
		return (tile.Source <= document.ExtraTileSets.size() &&
			(document.ExtraTileSets[tile.Source - 1].Flags & Formats::ExtraTileSetRecord::Embedded) == 0);
	}

	void TerrainRules::Load(StringView tileSet)
	{
		_terrains.clear();
		_slopes.clear();
		_current = 0;
		_revision++;

		Json::Value doc;
		if (RulesFile::Read(tileSet, doc)) {
			const Json::Value& terrains = doc["terrains"];
			if (terrains.isArray()) {
				for (const Json::Value& value : terrains) {
					if (!value.isObject()) {
						continue;
					}
					std::int32_t index = AddTerrain(StringView(value.get("name", "Ground").asCString()));
					Terrain& terrain = _terrains[index];
					const Json::Value& patterns = value["patterns"];
					if (!patterns.isObject()) {
						continue;
					}
					for (const std::string& name : patterns.getMemberNames()) {
						std::int32_t pattern = std::atoi(name.c_str());
						if (pattern < 0 || pattern > 255) {
							continue;
						}
						auto& tiles = terrain.ByPattern[Reduce(std::uint8_t(pattern))];
						for (const Json::Value& tile : patterns[name]) {
							if (tile.isObject()) {
								RuleTile rule;
								rule.TileSet = String(tile.get("s", "").asCString());
								rule.Index = std::uint16_t(tile.get("i", 0).asUInt());
								rule.Flags = std::uint8_t(tile.get("f", 0).asUInt() & 0x03);
								tiles.push_back(std::move(rule));
							}
						}
					}
				}
			}
			const Json::Value& slopes = doc["slopes"];
			if (slopes.isObject()) {
				for (const std::string& name : slopes.getMemberNames()) {
					std::int32_t index = std::atoi(name.c_str());
					const Json::Value& value = slopes[name];
					if (index <= 0 || index > UINT16_MAX || !value.isObject()) {
						continue;
					}
					SlopeTag tag;
					tag.Excluded = value.get("x", false).asBool();
					tag.Left = std::clamp(value.get("l", 0.0).asFloat(), 0.0f, 32.0f);
					tag.Right = std::clamp(value.get("r", 0.0).asFloat(), 0.0f, 32.0f);
					SetSlope(std::uint16_t(index), &tag);
				}
			}
		}
		if (_terrains.empty()) {
			AddTerrain("Ground"_s);
		}
		_resolvedFor = {};
		if (_document != nullptr) {
			Resolve(*_document);
		}
	}

	bool TerrainRules::Save(StringView tileSet) const
	{
		// The other parts of the rules stay as they are
		Json::Value doc;
		RulesFile::Read(tileSet, doc);
		doc.removeMember("terrain");

		Json::Value terrains(Json::arrayValue);
		bool any = false;
		for (const Terrain& terrain : _terrains) {
			Json::Value value(Json::objectValue);
			value["name"] = std::string(terrain.Name.data(), terrain.Name.size());
			Json::Value patterns(Json::objectValue);
			for (std::int32_t i = 0; i < 256; i++) {
				if (terrain.ByPattern[i].empty()) {
					continue;
				}
				Json::Value& list = patterns[std::to_string(i)];
				list = Json::Value(Json::arrayValue);
				for (const RuleTile& tile : terrain.ByPattern[i]) {
					Json::Value item(Json::objectValue);
					item["i"] = tile.Index;
					if (tile.Flags != 0) {
						item["f"] = tile.Flags;
					}
					if (!tile.TileSet.empty()) {
						item["s"] = std::string(tile.TileSet.data(), tile.TileSet.size());
					}
					list.append(item);
				}
				any = true;
			}
			value["patterns"] = patterns;
			terrains.append(value);
		}
		// A single terrain without any rule and without a name of its own is as if there were none
		if (!any && _terrains.size() == 1 && _terrains[0].Name == "Ground"_s) {
			doc.removeMember("terrains");
		} else {
			doc["terrains"] = terrains;
		}

		if (_slopes.empty()) {
			doc.removeMember("slopes");
		} else {
			Json::Value slopes(Json::objectValue);
			for (const auto& [index, tag] : _slopes) {
				Json::Value value(Json::objectValue);
				if (tag.Excluded) {
					value["x"] = true;
				} else {
					value["l"] = tag.Left;
					value["r"] = tag.Right;
				}
				slopes[std::to_string(index)] = value;
			}
			doc["slopes"] = slopes;
		}
		return RulesFile::Write(tileSet, doc);
	}

	String TerrainRules::GetExtraTileSetsKey(const LevelDocument& document)
	{
		String key;
		for (const Formats::ExtraTileSetRecord& tileSet : document.ExtraTileSets) {
			key += StringUtils::lowercase(tileSet.Name) + "|"_s + String(std::to_string(tileSet.Offset).c_str()) + "|"_s +
				String(std::to_string(tileSet.Count).c_str()) + ((tileSet.Flags & Formats::ExtraTileSetRecord::Embedded) != 0 ? ":e;"_s : ";"_s);
		}
		return key;
	}

	void TerrainRules::Resolve(const LevelDocument& document)
	{
		_document = &document;
		String key = GetExtraTileSetsKey(document);
		if (key == _resolvedFor && !_resolvedFor.empty()) {
			return;
		}
		_resolvedFor = (key.empty() ? String(";"_s) : key);
		for (Terrain& terrain : _terrains) {
			ResolveTerrain(terrain);
		}
		_revision++;
	}

	void TerrainRules::ResolveTerrain(Terrain& terrain) const
	{
		terrain.Resolved.clear();
		terrain.Resolved.resize(256);
		for (std::int32_t pattern = 0; pattern < 256; pattern++) {
			for (const RuleTile& tile : terrain.ByPattern[pattern]) {
				if (tile.TileSet.empty()) {
					terrain.Resolved[pattern].push_back(TileRef{ tile.Index, TileRef::DefaultTileSet, tile.Flags });
					continue;
				}
				if (_document == nullptr) {
					continue;
				}
				// The first slot of the tile set that has the tile, if the level has the tile set at all
				for (std::size_t slot = 0; slot < _document->ExtraTileSets.size(); slot++) {
					const Formats::ExtraTileSetRecord& record = _document->ExtraTileSets[slot];
					if ((record.Flags & Formats::ExtraTileSetRecord::Embedded) == 0 && StringUtils::lowercase(record.Name) == StringUtils::lowercase(tile.TileSet) &&
						tile.Index >= record.Offset && tile.Index < record.Offset + record.Count) {
						terrain.Resolved[pattern].push_back(TileRef{ std::uint16_t(tile.Index - record.Offset), std::uint8_t(slot + 1), tile.Flags });
						break;
					}
				}
			}
		}
	}

	StringView TerrainRules::GetTerrainName(std::int32_t index) const
	{
		return (index >= 0 && index < std::int32_t(_terrains.size()) ? StringView(_terrains[index].Name) : StringView());
	}

	void TerrainRules::RenameTerrain(std::int32_t index, StringView name)
	{
		if (index >= 0 && index < std::int32_t(_terrains.size()) && !name.empty()) {
			_terrains[index].Name = name;
			_revision++;
		}
	}

	std::int32_t TerrainRules::AddTerrain(StringView name)
	{
		Terrain& terrain = _terrains.emplace_back();
		terrain.Name = name;
		terrain.ByPattern.resize(256);
		terrain.Resolved.resize(256);
		_revision++;
		return std::int32_t(_terrains.size()) - 1;
	}

	void TerrainRules::RemoveTerrain(std::int32_t index)
	{
		if (_terrains.size() <= 1 || index < 0 || index >= std::int32_t(_terrains.size())) {
			return;
		}
		_terrains.erase(_terrains.begin() + index);
		_current = std::min(_current, std::int32_t(_terrains.size()) - 1);
		_revision++;
	}

	void TerrainRules::SetCurrentTerrain(std::int32_t index)
	{
		if (index >= 0 && index < std::int32_t(_terrains.size()) && index != _current) {
			_current = index;
			_revision++;
		}
	}

	const SmallVector<TileRef, 0>* TerrainRules::Find(std::uint8_t neighbors) const
	{
		const Terrain& terrain = _terrains[_current];
		const auto& tiles = terrain.Resolved[Reduce(neighbors)];
		return (tiles.empty() ? nullptr : &tiles);
	}

	void TerrainRules::Set(std::uint8_t pattern, ArrayView<const TileRef> tiles)
	{
		Terrain& terrain = _terrains[_current];
		std::uint8_t reduced = Reduce(pattern);
		auto& list = terrain.ByPattern[reduced];
		list.clear();
		for (const TileRef& tile : tiles) {
			if (tile.Source == TileRef::DefaultTileSet) {
				if (!tile.IsEmpty() && (tile.Flags & Formats::TileRecord::ModifierMask) == 0) {
					list.push_back(RuleTile{ String(), tile.Index, std::uint8_t(tile.Flags & 0x03) });
				}
			} else if (_document != nullptr && IsRuleTile(*_document, tile)) {
				const Formats::ExtraTileSetRecord& record = _document->ExtraTileSets[tile.Source - 1];
				list.push_back(RuleTile{ record.Name, std::uint16_t(tile.Index + record.Offset), std::uint8_t(tile.Flags & 0x03) });
			}
		}
		ResolveTerrain(terrain);
		_revision++;
	}

	void TerrainRules::Clear()
	{
		if (_terrains.empty()) {
			return;
		}
		Terrain& terrain = _terrains[_current];
		for (auto& list : terrain.ByPattern) {
			list.clear();
		}
		for (auto& list : terrain.Resolved) {
			list.clear();
		}
		_revision++;
	}

	std::int32_t TerrainRules::GetCount() const
	{
		std::int32_t count = 0;
		for (const auto& list : _terrains[_current].ByPattern) {
			count += (list.empty() ? 0 : 1);
		}
		return count;
	}

	const TerrainRules::SlopeTag* TerrainRules::FindSlope(std::uint16_t index) const
	{
		auto it = std::lower_bound(_slopes.begin(), _slopes.end(), index, [](const auto& item, std::uint16_t value) { return item.first < value; });
		return (it != _slopes.end() && it->first == index ? &it->second : nullptr);
	}

	void TerrainRules::SetSlope(std::uint16_t index, const SlopeTag* tag)
	{
		auto it = std::lower_bound(_slopes.begin(), _slopes.end(), index, [](const auto& item, std::uint16_t value) { return item.first < value; });
		bool exists = (it != _slopes.end() && it->first == index);
		if (tag == nullptr) {
			if (exists) {
				_slopes.erase(it);
			}
		} else if (exists) {
			it->second = *tag;
		} else {
			_slopes.insert(it, { index, *tag });
		}
		_revision++;
	}

	void TerrainRules::GetSlopeTiles(SmallVectorImpl<std::uint16_t>& tiles) const
	{
		tiles.clear();
		for (const auto& item : _slopes) {
			tiles.push_back(item.first);
		}
	}
}

#endif
