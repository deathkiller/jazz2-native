#include "TileSetDataCache.h"

#if defined(WITH_EDITOR)

#include "ContentTileSetLibrary.h"
#include "../../Formats/TileSetFile.h"

#include <IO/FileSystem.h>
#include <IO/MemoryStream.h>

#include <cstdint>

using namespace Death::IO;

namespace Jazz2::Editor
{
	TileSetDataCache::TileSetDataCache()
		: _levelTilesGeneration(1), _useCounter(0)
	{
	}

	TileSetDataCache::~TileSetDataCache()
	{
	}

	const Formats::TileSetData* TileSetDataCache::Get(const LevelDocument& document, StringView name)
	{
		if (name.hasPrefix(':')) {
			// Stored in the level itself, an undo can change it any time
			const Formats::ExtraTileSetRecord* record = nullptr;
			for (const Formats::ExtraTileSetRecord& tileSet : document.ExtraTileSets) {
				if ((tileSet.Flags & Formats::ExtraTileSetRecord::Embedded) != 0 && tileSet.Name == name) {
					record = &tileSet;
					break;
				}
			}
			std::uint64_t hash = 0;
			bool hashed = false;
			for (std::size_t i = 0; i < _entries.size(); i++) {
				CachedTileSet& entry = _entries[i];
				if (entry.Name == name) {
					if (record != nullptr && entry.ByteCount == record->Data.size()) {
						if (entry.Generation == _levelTilesGeneration && entry.Bytes == record->Data.data()) {
							return entry.Data.get();
						}
						// A change of the same size is copied into the same bytes, as by SetLevelTilesCommand, so only the
						// hash tells, it's computed only after a change of tiles
						hash = HashBytes(record->Data.data(), record->Data.size());
						hashed = true;
						if (hash == entry.Hash) {
							entry.Generation = _levelTilesGeneration;
							entry.Bytes = record->Data.data();
							return entry.Data.get();
						}
					}
					_entries.erase(_entries.begin() + i);
					break;
				}
			}
			if (record == nullptr) {
				return nullptr;
			}
			CachedTileSet& cached = _entries.emplace_back();
			cached.Name = name;
			cached.Bytes = record->Data.data();
			cached.ByteCount = record->Data.size();
			cached.Hash = (hashed ? hash : HashBytes(record->Data.data(), record->Data.size()));
			cached.Generation = _levelTilesGeneration;
			MemoryStream ms(record->Data.data(), std::int64_t(record->Data.size()));
			auto tileSet = std::make_unique<Formats::TileSetData>();
			if (Formats::TileSetFile::Read(ms, *tileSet)) {
				cached.Data = Death::move(tileSet);
			}
			return cached.Data.get();
		}

		_useCounter++;
		for (CachedTileSet& cached : _entries) {
			if (cached.Name == name) {
				cached.LastUsed = _useCounter;
				return cached.Data.get();
			}
		}
		// Also when it can't be read, so it isn't tried again and again
		CachedTileSet& cached = _entries.emplace_back();
		cached.Name = name;
		cached.LastUsed = _useCounter;
		String path = ContentTileSetLibrary::FindTileSetFile(name);
		if (!path.empty()) {
			auto s = fs::Open(path, FileAccess::Read);
			auto tileSet = std::make_unique<Formats::TileSetData>();
			if (s->IsValid() && Formats::TileSetFile::Read(*s, *tileSet)) {
				cached.Data = Death::move(tileSet);
			}
		}
		const Formats::TileSetData* result = cached.Data.get();
		TrimUnused(document);
		return result;
	}

	void TileSetDataCache::TrimUnused(const LevelDocument& document)
	{
		// A tile set of the size of the largest one takes megabytes, so only a few of those the level doesn't use are kept,
		// as the tile sets offered to be added to it, the least recently asked for are forgotten
		auto isUsed = [&document](StringView name) {
			if (name == document.Properties.DefaultTileSet) {
				return true;
			}
			for (const Formats::ExtraTileSetRecord& tileSet : document.ExtraTileSets) {
				if ((tileSet.Flags & Formats::ExtraTileSetRecord::Embedded) == 0 && tileSet.Name == name) {
					return true;
				}
			}
			return false;
		};
		while (true) {
			std::size_t unusedCount = 0, oldest = SIZE_MAX;
			for (std::size_t i = 0; i < _entries.size(); i++) {
				const CachedTileSet& entry = _entries[i];
				if (entry.Name.hasPrefix(':') || isUsed(entry.Name)) {
					continue;
				}
				unusedCount++;
				if (oldest == SIZE_MAX || entry.LastUsed < _entries[oldest].LastUsed) {
					oldest = i;
				}
			}
			if (unusedCount <= MaxUnusedTileSets) {
				break;
			}
			_entries.erase(_entries.begin() + oldest);
		}
	}

	void TileSetDataCache::Invalidate(StringView name)
	{
		for (std::size_t i = 0; i < _entries.size(); i++) {
			if (_entries[i].Name == name) {
				_entries.erase(_entries.begin() + i);
				break;
			}
		}
	}

	void TileSetDataCache::InvalidateLevelTileSets()
	{
		for (std::size_t i = _entries.size(); i > 0; i--) {
			if (_entries[i - 1].Name.hasPrefix(':')) {
				_entries.erase(_entries.begin() + (i - 1));
			}
		}
		_levelTilesGeneration++;
	}

	std::uint64_t TileSetDataCache::HashBytes(const void* data, std::size_t size, std::uint64_t hash)
	{
		const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
		for (std::size_t i = 0; i < size; i++) {
			hash = (hash ^ bytes[i]) * 1099511628211ull;
		}
		return hash;
	}
}

#endif
