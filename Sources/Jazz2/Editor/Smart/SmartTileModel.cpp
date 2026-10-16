#include "SmartTileModel.h"

#if defined(WITH_EDITOR)

#include "../../ContentResolver.h"
#include "../../Formats/LevelFile.h"

#include <Containers/DateTime.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>

#include <algorithm>
#include <cmath>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		// Levels whose tile set is checked in a frame, and of them read whole, which takes longer
		constexpr std::int32_t MaxChecksPerFrame = 8;
		constexpr std::int32_t MaxReadsPerFrame = 1;

		void CollectLevels(StringView path, SmallVectorImpl<String>& files)
		{
			if (!fs::DirectoryExists(path)) {
				return;
			}
			for (auto item : fs::Directory(path, fs::EnumerationOptions::SkipDirectories)) {
				if (StringUtils::lowercase(fs::GetExtension(item)) == "j2l"_s) {
					files.push_back(item);
				}
			}
			for (auto item : fs::Directory(path, fs::EnumerationOptions::SkipFiles)) {
				CollectLevels(item, files);
			}
		}

		// Whether a level uses a tile set as its default one, read only from its header
		bool UsesTileSet(StringView path, StringView tileSet)
		{
			auto s = fs::Open(path, FileAccess::Read);
			Formats::LevelHeader header;
			if (!s->IsValid() || !Formats::LevelFile::ReadHeader(*s, header)) {
				return false;
			}
			Formats::LevelReader reader(*s, header);
			Formats::LevelProperties properties;
			reader.ReadProperties(properties);
			return (reader.IsValid() && StringUtils::lowercase(properties.DefaultTileSet) == StringUtils::lowercase(tileSet));
		}

		bool IsOccupied(const TileRef& tile)
		{
			return !tile.IsEmpty() && (tile.Flags & Formats::TileRecord::ModifierMask) != Formats::TileRecord::ModifierInvisible;
		}
	}

	SmartTileModel::SmartTileModel()
		: _library(nullptr), _nextFile(0), _levelCount(0), _dirty(false), _corpusSignature(0), _cacheWritten(false)
	{
	}

	void SmartTileModel::Request(StringView tileSet, ITileSetLibrary& library)
	{
		if (!_tileSet.empty() && StringUtils::lowercase(_tileSet) == StringUtils::lowercase(tileSet)) {
			return;
		}
		_tileSet = tileSet;
		_library = &library;
		_files.clear();
		_nextFile = 0;
		_levelCount = 0;
		for (auto& counts : _counts) {
			counts.clear();
		}
		_rightPairs.clear();
		_downPairs.clear();
		_dirty = true;
		_rules.Load(tileSet);

		// The levels of the game, the content and the user content, each file once
		auto& resolver = ContentResolver::Get();
		CollectLevels(fs::CombinePath(resolver.GetCachePath(), "Episodes"_s), _files);
		CollectLevels(fs::CombinePath(resolver.GetContentPath(), "Episodes"_s), _files);
		StringView userContent = resolver.GetUserContentPath();
		if (!userContent.empty()) {
			CollectLevels(fs::CombinePath(userContent, "Episodes"_s), _files);
		}
		std::sort(_files.begin(), _files.end());
		_files.erase(std::unique(_files.begin(), _files.end()), _files.end());

		// The levels as they are now, any of them changed, added or removed makes them read again
		std::uint64_t hash = 0xCBF29CE484222325ull;
		auto mix = [&hash](const void* data, std::size_t size) {
			const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
			for (std::size_t i = 0; i < size; i++) {
				hash = (hash ^ bytes[i]) * 0x100000001B3ull;
			}
		};
		String lowerTileSet = StringUtils::lowercase(tileSet);
		mix(lowerTileSet.data(), lowerTileSet.size());
		for (const String& file : _files) {
			mix(file.data(), file.size());
			std::int64_t size = fs::GetFileSize(file);
			std::int64_t time = fs::GetLastModificationTime(file).ToUnixMilliseconds();
			mix(&size, sizeof(size));
			mix(&time, sizeof(time));
		}
		_corpusSignature = hash;
		_cacheWritten = false;
		if (LoadCache()) {
			_nextFile = _files.size();
			_cacheWritten = true;
		}
	}

	void SmartTileModel::Update()
	{
		std::int32_t checks = 0, reads = 0;
		while (_nextFile < _files.size() && checks < MaxChecksPerFrame && reads < MaxReadsPerFrame) {
			const String& path = _files[_nextFile++];
			checks++;
			if (!UsesTileSet(path, _tileSet)) {
				continue;
			}
			reads++;
			auto s = fs::Open(path, FileAccess::Read);
			LevelDocument document;
			if (s->IsValid() && document.Load(*s, _library)) {
				AddLevel(document, 1);
			}
		}
		if (_nextFile >= _files.size() && !_cacheWritten) {
			_cacheWritten = true;
			SaveCache();
		}
	}

	String SmartTileModel::GetCachePath() const
	{
		String name = StringUtils::lowercase(_tileSet);
		for (char& c : name) {
			if (c == '/' || c == '\\' || c == ':') {
				c = '_';
			}
		}
		String fileName = name + ".adj"_s;
		return fs::CombinePath({ ContentResolver::Get().GetCachePath(), "Editor"_s, "Adjacency"_s, StringView(fileName) });
	}

	// "J2ADJ" + version, the signature of the levels, then the counts and the pairs, little-endian
	static constexpr std::uint64_t CacheMagic = 0x01004A44413232ull;

	bool SmartTileModel::LoadCache()
	{
		auto s = fs::Open(GetCachePath(), FileAccess::Read);
		if (!s->IsValid() || s->ReadValueAsLE<std::uint64_t>() != CacheMagic || s->ReadValueAsLE<std::uint64_t>() != _corpusSignature) {
			return false;
		}
		auto read = [&]() {
			_levelCount = s->ReadValueAsLE<std::int32_t>();
			// An entry takes 8 bytes at least, more of them than the file can hold is a damaged file
			std::uint32_t maxEntries = std::uint32_t(s->GetSize() / 8);
			for (auto& counts : _counts) {
				std::uint32_t size = s->ReadValueAsLE<std::uint32_t>();
				if (size > maxEntries) {
					return false;
				}
				counts.reserve(size);
				for (std::uint32_t i = 0; i < size; i++) {
					std::uint32_t key = s->ReadValueAsLE<std::uint32_t>();
					counts[key] = s->ReadValueAsLE<std::uint32_t>();
				}
			}
			for (auto* pairs : { &_rightPairs, &_downPairs }) {
				std::uint32_t size = s->ReadValueAsLE<std::uint32_t>();
				if (size > maxEntries) {
					return false;
				}
				pairs->reserve(size);
				for (std::uint32_t i = 0; i < size; i++) {
					std::uint64_t key = s->ReadValueAsLE<std::uint64_t>();
					(*pairs)[key] = s->ReadValueAsLE<std::uint32_t>();
				}
			}
			return (s->GetPosition() == s->GetSize());
		};
		if (!read()) {
			// Not whole, it's learned again
			LOGW("Cache of the tiles of \"{}\" is damaged", _tileSet);
			_levelCount = 0;
			for (auto& counts : _counts) {
				counts.clear();
			}
			_rightPairs.clear();
			_downPairs.clear();
			return false;
		}
		_dirty = true;
		LOGI("Tiles of \"{}\" known from the cache, as {} levels use them", _tileSet, _levelCount);
		return true;
	}

	void SmartTileModel::SaveCache()
	{
		String path = GetCachePath();
		fs::CreateDirectories(fs::GetDirectoryName(path));
		String tempPath = path + ".tmp"_s;
		{
			auto s = fs::Open(tempPath, FileAccess::Write);
			if (!s->IsValid()) {
				return;
			}
			s->WriteValueAsLE<std::uint64_t>(CacheMagic);
			s->WriteValueAsLE<std::uint64_t>(_corpusSignature);
			s->WriteValueAsLE<std::int32_t>(_levelCount);
			for (const auto& counts : _counts) {
				s->WriteValueAsLE<std::uint32_t>(std::uint32_t(counts.size()));
				for (const auto& [key, count] : counts) {
					s->WriteValueAsLE<std::uint32_t>(key);
					s->WriteValueAsLE<std::uint32_t>(count);
				}
			}
			for (const auto* pairs : { &_rightPairs, &_downPairs }) {
				s->WriteValueAsLE<std::uint32_t>(std::uint32_t(pairs->size()));
				for (const auto& [key, count] : *pairs) {
					s->WriteValueAsLE<std::uint64_t>(key);
					s->WriteValueAsLE<std::uint32_t>(count);
				}
			}
		}
		if (fs::FileExists(path)) {
			fs::RemoveFile(path);
		}
		if (!fs::Move(tempPath, path)) {
			fs::RemoveFile(tempPath);
		}
	}

	float SmartTileModel::GetProgress() const
	{
		return (_files.empty() ? 1.0f : float(_nextFile) / float(_files.size()));
	}

	void SmartTileModel::AddLevel(const LevelDocument& document, std::uint32_t weight)
	{
		_levelCount++;
		_dirty = true;
		for (const DocumentLayer& layer : document.Layers) {
			std::int32_t width = layer.GetWidth(), height = layer.GetHeight();
			auto& counts = _counts[layer.IsSpriteLayer() ? 1 : 0];
			for (std::int32_t y = 0; y < height; y++) {
				for (std::int32_t x = 0; x < width; x++) {
					const TileRef& tile = layer.GetTile(x, y);
					if (!IsModelTile(tile)) {
						continue;
					}
					// Outside of the layer counts as not empty, so tiles at its edges don't look like they're in the open
					auto occupied = [&](std::int32_t dx, std::int32_t dy) {
						std::int32_t nx = x + dx, ny = y + dy;
						return (nx < 0 || ny < 0 || nx >= width || ny >= height || IsOccupied(layer.GetTile(nx, ny)));
					};
					std::uint8_t pattern = std::uint8_t((occupied(-1, -1) ? UpLeft : 0) | (occupied(0, -1) ? Up : 0) | (occupied(1, -1) ? UpRight : 0) |
						(occupied(-1, 0) ? Left : 0) | (occupied(1, 0) ? Right : 0) |
						(occupied(-1, 1) ? DownLeft : 0) | (occupied(0, 1) ? Down : 0) | (occupied(1, 1) ? DownRight : 0));
					std::uint32_t key = ToKey(tile);
					counts[(std::uint32_t(pattern) << 18) | key] += weight;

					if (x + 1 < width && IsModelTile(layer.GetTile(x + 1, y))) {
						_rightPairs[(std::uint64_t(key) << 32) | ToKey(layer.GetTile(x + 1, y))] += weight;
					}
					if (y + 1 < height && IsModelTile(layer.GetTile(x, y + 1))) {
						_downPairs[(std::uint64_t(key) << 32) | ToKey(layer.GetTile(x, y + 1))] += weight;
					}
				}
			}
		}
	}

	bool SmartTileModel::Pick(std::uint8_t neighbors, bool spriteLayer, const TileRef* left, const TileRef* up, const TileRef* right, const TileRef* down,
		std::uint32_t hash, TileRef& result, const TileRef* keep)
	{
		// Tiles chosen by hand go first, the one there stays if it's one of them
		if (const SmallVector<TileRef, 0>* ruled = _rules.Find(neighbors)) {
			if (keep != nullptr) {
				for (const TileRef& tile : *ruled) {
					if (tile.Index == keep->Index && tile.Source == keep->Source && (tile.Flags & 0x03) == (keep->Flags & 0x03)) {
						result = *keep;
						return true;
					}
				}
			}
			result = (*ruled)[hash % std::uint32_t(ruled->size())];
			return true;
		}

		if (_dirty) {
			Rebuild();
		}

		// Those of the layer kind first, then of the others, then with the neighbors at the sides only
		std::int32_t kind = (spriteLayer ? 1 : 0);
		std::uint8_t sides = std::uint8_t(((neighbors & Up) != 0 ? 1 : 0) | ((neighbors & Right) != 0 ? 2 : 0) | ((neighbors & Down) != 0 ? 4 : 0) | ((neighbors & Left) != 0 ? 8 : 0));
		const SmallVector<Candidate, 0>* candidates = &_byPattern[kind][neighbors];
		if (candidates->empty()) {
			candidates = &_byPattern[1 - kind][neighbors];
		}
		if (candidates->empty()) {
			candidates = &_bySides[kind][sides];
		}
		if (candidates->empty()) {
			candidates = &_bySides[1 - kind][sides];
		}
		if (candidates->empty()) {
			return false;
		}

		auto pairCount = [](const std::unordered_map<std::uint64_t, std::uint32_t>& pairs, std::uint32_t first, std::uint32_t second) -> std::uint32_t {
			auto it = pairs.find((std::uint64_t(first) << 32) | second);
			return (it != pairs.end() ? it->second : 0);
		};
		std::uint32_t leftKey = (left != nullptr && IsModelTile(*left) ? ToKey(*left) : UINT32_MAX);
		std::uint32_t upKey = (up != nullptr && IsModelTile(*up) ? ToKey(*up) : UINT32_MAX);
		std::uint32_t rightKey = (right != nullptr && IsModelTile(*right) ? ToKey(*right) : UINT32_MAX);
		std::uint32_t downKey = (down != nullptr && IsModelTile(*down) ? ToKey(*down) : UINT32_MAX);
		std::uint32_t keepKey = (keep != nullptr && IsModelTile(*keep) ? ToKey(*keep) : UINT32_MAX);

		// How often the levels used it so, and how well it goes with the tiles next to it, a little varied
		float bestScore = -1e30f;
		std::uint32_t bestTile = (*candidates)[0].Tile;
		std::uint32_t total = 0;
		for (const Candidate& candidate : *candidates) {
			total += candidate.Count;
		}
		constexpr std::size_t MaxCandidates = 24;
		for (std::size_t i = 0; i < candidates->size() && i < MaxCandidates; i++) {
			const Candidate& candidate = (*candidates)[i];
			float score = std::log(float(candidate.Count) / float(total));
			if (leftKey != UINT32_MAX) {
				score += 0.6f * std::log(1.0f + float(pairCount(_rightPairs, leftKey, candidate.Tile)));
			}
			if (upKey != UINT32_MAX) {
				score += 0.6f * std::log(1.0f + float(pairCount(_downPairs, upKey, candidate.Tile)));
			}
			if (rightKey != UINT32_MAX) {
				score += 0.6f * std::log(1.0f + float(pairCount(_rightPairs, candidate.Tile, rightKey)));
			}
			if (downKey != UINT32_MAX) {
				score += 0.6f * std::log(1.0f + float(pairCount(_downPairs, candidate.Tile, downKey)));
			}
			// Tiles about as good take turns, the one there stays if it fits
			std::uint32_t h = (hash ^ (candidate.Tile * 0x9E3779B1u)) * 0x85EBCA6Bu;
			h ^= h >> 13;
			score += 0.8f * float(h & 0xFFFF) / 65535.0f;
			if (candidate.Tile == keepKey) {
				score += 3.0f;
			}
			if (score > bestScore) {
				bestScore = score;
				bestTile = candidate.Tile;
			}
		}
		result = FromKey(bestTile);
		return true;
	}

	std::uint32_t SmartTileModel::GetSurfaceUsage(const TileRef& tile)
	{
		if (_dirty) {
			Rebuild();
		}
		auto it = _surfaceUsage.find(ToKey(tile));
		return (it != _surfaceUsage.end() ? it->second : 0);
	}

	std::uint32_t SmartTileModel::GetCount(std::uint8_t neighbors, bool spriteLayer, const TileRef& tile) const
	{
		if (!IsModelTile(tile)) {
			return 0;
		}
		std::uint32_t key = (std::uint32_t(neighbors) << 18) | ToKey(tile);
		auto it = _counts[spriteLayer ? 1 : 0].find(key);
		std::uint32_t count = (it != _counts[spriteLayer ? 1 : 0].end() ? it->second : 0);
		// The other layers count much less, they have tiles in front of or behind the ground
		auto other = _counts[spriteLayer ? 0 : 1].find(key);
		return count + (other != _counts[spriteLayer ? 0 : 1].end() ? other->second / 16 : 0);
	}

	bool SmartTileModel::Fits(std::uint8_t neighbors, bool spriteLayer, const TileRef& tile) const
	{
		if (const SmallVector<TileRef, 0>* ruled = _rules.Find(neighbors)) {
			for (const TileRef& candidate : *ruled) {
				if (candidate.Index == tile.Index && candidate.Source == tile.Source && (candidate.Flags & 0x03) == (tile.Flags & 0x03)) {
					return true;
				}
			}
			return false;
		}
		return (GetCount(neighbors, spriteLayer, tile) > 0);
	}

	std::uint32_t SmartTileModel::GetRightPair(const TileRef& left, const TileRef& right) const
	{
		if (!IsModelTile(left) || !IsModelTile(right)) {
			return 0;
		}
		auto it = _rightPairs.find((std::uint64_t(ToKey(left)) << 32) | ToKey(right));
		return (it != _rightPairs.end() ? it->second : 0);
	}

	std::uint32_t SmartTileModel::GetDownPair(const TileRef& up, const TileRef& down) const
	{
		if (!IsModelTile(up) || !IsModelTile(down)) {
			return 0;
		}
		auto it = _downPairs.find((std::uint64_t(ToKey(up)) << 32) | ToKey(down));
		return (it != _downPairs.end() ? it->second : 0);
	}

	std::int32_t SmartTileModel::Suggest(const TileRef* left, const TileRef* up, const TileRef* right, const TileRef* down, std::int32_t maxCount,
		SmallVectorImpl<TileRef>& result)
	{
		result.clear();
		if (_dirty) {
			Rebuild();
		}
		struct Score
		{
			std::int32_t Hits = 0;
			float Value = 0.0f;
		};
		std::unordered_map<std::uint32_t, Score> scores;
		// Right of the tile on the left, left of the one on the right, below the one above, above the one below
		const TileRef* neighbors[4] = { left, right, up, down };
		for (std::int32_t side = 0; side < 4; side++) {
			const TileRef* neighbor = neighbors[side];
			if (neighbor == nullptr || !IsModelTile(*neighbor)) {
				continue;
			}
			auto it = _neighborsOf[side].find(ToKey(*neighbor));
			if (it == _neighborsOf[side].end()) {
				continue;
			}
			for (const Candidate& candidate : it->second) {
				Score& score = scores[candidate.Tile];
				score.Hits++;
				score.Value += std::log(1.0f + float(candidate.Count));
			}
		}
		SmallVector<std::pair<std::uint32_t, Score>, 0> sorted;
		for (const auto& [tile, score] : scores) {
			sorted.push_back({ tile, score });
		}
		std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
			if (a.second.Hits != b.second.Hits) {
				return a.second.Hits > b.second.Hits;
			}
			return (a.second.Value != b.second.Value ? a.second.Value > b.second.Value : a.first < b.first);
		});
		for (std::size_t i = 0; i < sorted.size() && std::int32_t(result.size()) < maxCount; i++) {
			result.push_back(FromKey(sorted[i].first));
		}
		return std::int32_t(result.size());
	}

	void SmartTileModel::GetTopTiles(std::uint8_t pattern, bool spriteLayer, std::int32_t maxCount, float minShare, SmallVectorImpl<TileRef>& result)
	{
		result.clear();
		std::uint8_t reduced = TerrainRules::Reduce(pattern);
		std::unordered_map<std::uint32_t, std::uint32_t> totals;
		for (const auto& [key, count] : _counts[spriteLayer ? 1 : 0]) {
			if (TerrainRules::Reduce(std::uint8_t(key >> 18)) == reduced) {
				totals[key & 0x3FFFF] += count;
			}
		}
		SmallVector<Candidate, 0> sorted;
		for (const auto& [tile, count] : totals) {
			sorted.push_back(Candidate{ tile, count });
		}
		std::sort(sorted.begin(), sorted.end(), [](const Candidate& a, const Candidate& b) { return (a.Count != b.Count ? a.Count > b.Count : a.Tile < b.Tile); });
		for (const Candidate& candidate : sorted) {
			if (std::int32_t(result.size()) >= maxCount || float(candidate.Count) < float(sorted[0].Count) * minShare) {
				break;
			}
			result.push_back(FromKey(candidate.Tile));
		}
	}

	bool SmartTileModel::IsModelTile(const TileRef& tile)
	{
		return (tile.Source == TileRef::DefaultTileSet && !tile.IsEmpty() &&
			(tile.Flags & Formats::TileRecord::ModifierMask) == 0);
	}

	std::uint32_t SmartTileModel::ToKey(const TileRef& tile)
	{
		return std::uint32_t(tile.Index) | (std::uint32_t(tile.Flags & (Formats::TileRecord::FlipX | Formats::TileRecord::FlipY)) << 16);
	}

	TileRef SmartTileModel::FromKey(std::uint32_t key)
	{
		return TileRef{ std::uint16_t(key & 0xFFFF), TileRef::DefaultTileSet, std::uint8_t((key >> 16) & 0x03) };
	}

	void SmartTileModel::Rebuild()
	{
		_dirty = false;
		_surfaceUsage.clear();
		for (const auto& counts : _counts) {
			for (const auto& [key, count] : counts) {
				std::uint8_t pattern = std::uint8_t(key >> 18);
				if ((pattern & Up) == 0 && (pattern & Down) != 0) {
					_surfaceUsage[key & 0x3FFFF] += count;
				}
			}
		}
		for (std::int32_t kind = 0; kind < 2; kind++) {
			for (auto& list : _byPattern[kind]) {
				list.clear();
			}
			for (auto& list : _bySides[kind]) {
				list.clear();
			}
			std::unordered_map<std::uint32_t, std::uint32_t> sideCounts;
			for (const auto& [key, count] : _counts[kind]) {
				std::uint8_t pattern = std::uint8_t(key >> 18);
				std::uint32_t tile = key & 0x3FFFF;
				_byPattern[kind][pattern].push_back(Candidate{ tile, count });
				std::uint32_t sides = std::uint32_t(((pattern & Up) != 0 ? 1 : 0) | ((pattern & Right) != 0 ? 2 : 0) | ((pattern & Down) != 0 ? 4 : 0) | ((pattern & Left) != 0 ? 8 : 0));
				sideCounts[(sides << 18) | tile] += count;
			}
			for (const auto& [key, count] : sideCounts) {
				_bySides[kind][key >> 18].push_back(Candidate{ key & 0x3FFFF, count });
			}
			auto byCount = [](const Candidate& a, const Candidate& b) { return (a.Count != b.Count ? a.Count > b.Count : a.Tile < b.Tile); };
			for (auto& list : _byPattern[kind]) {
				std::sort(list.begin(), list.end(), byCount);
			}
			for (auto& list : _bySides[kind]) {
				std::sort(list.begin(), list.end(), byCount);
			}
		}

		// What is next to each tile, see Suggest()
		for (auto& lists : _neighborsOf) {
			lists.clear();
		}
		for (const auto& [key, count] : _rightPairs) {
			std::uint32_t first = std::uint32_t(key >> 32), second = std::uint32_t(key);
			_neighborsOf[0][first].push_back(Candidate{ second, count });
			_neighborsOf[1][second].push_back(Candidate{ first, count });
		}
		for (const auto& [key, count] : _downPairs) {
			std::uint32_t first = std::uint32_t(key >> 32), second = std::uint32_t(key);
			_neighborsOf[2][first].push_back(Candidate{ second, count });
			_neighborsOf[3][second].push_back(Candidate{ first, count });
		}
		auto byCount = [](const Candidate& a, const Candidate& b) { return (a.Count != b.Count ? a.Count > b.Count : a.Tile < b.Tile); };
		for (auto& lists : _neighborsOf) {
			for (auto& [key, list] : lists) {
				std::sort(list.begin(), list.end(), byCount);
			}
		}
	}
}

#endif
