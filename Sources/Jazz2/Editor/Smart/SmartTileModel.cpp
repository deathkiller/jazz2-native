#include "SmartTileModel.h"

#if defined(WITH_EDITOR)

#include "../Files/AtomicFile.h"
#include "../Files/ContentTileSetLibrary.h"
#include "../Common/ToolMath.h"
#include "../../ContentResolver.h"
#include "../../Formats/LevelFile.h"
#include "../../../nCine/Application.h"

#include <Containers/DateTime.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <Cryptography/xxHash.h>
#include <IO/FileSystem.h>

#if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)
#	include "../../../nCine/Threading/Thread.h"
#	include "../../../nCine/Threading/ThreadSync.h"
#	define SMART_TILES_THREAD
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		// Time a frame spends reading levels without threads, and taking in what was read
		constexpr float StepBudgetMs = 4.0f;
		constexpr float MergeBudgetMs = 3.0f;
		// What's next to each tile is made again at most this often, see EnsureNeighbors()
		constexpr float RebuildInterval = 1.0f;
		// How many times the level being edited counts, and how often it's learned again while it changes
		constexpr std::uint32_t DocumentWeight = 4;
		constexpr float DocumentRelearnInterval = 5.0f;
		// Time a frame spends learning the level being edited, a band of rows of a layer at a time
		constexpr float DocumentBudgetMs = 3.0f;
		constexpr std::int32_t DocumentBandRows = 16;
		// The cache is written as the levels are read too, after this many of them read whole or this long
		constexpr std::int32_t SaveEveryLevels = 32;
		constexpr float SaveInterval = 15.0f;
		// The bytes "22ADJ", a zero and the version, as a number written little-endian; then the tile set, then every
		// level found by its path, size and time with the counts it adds, all little-endian
		constexpr std::uint64_t CacheMagic = 0x02004A44413232ull;
		constexpr std::int64_t MaxCacheSize = 256 * 1024 * 1024;
		constexpr std::uint32_t MaxCachePath = 4096;
		constexpr std::int32_t MaxDirectories = 65536;
		// Levels the cache can have, more than a damaged one could make take gigabytes
		constexpr std::uint32_t MaxCacheRecords = 1 << 18;
		// Bytes of the cache read and taken in at a time, a large one is read over several frames without threads
		constexpr std::size_t CacheChunkBytes = 1 << 20;

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

		String GetCachePath(StringView tileSet)
		{
			// A path of a tile set is flattened to a name, with a hash of the whole name, so "a/b" and "a_b" get each a file
			String lowercase = StringUtils::lowercase(tileSet);
			String name = lowercase;
			for (char& c : name) {
				if (c == '/' || c == '\\' || c == ':') {
					c = '_';
				}
			}
			char hash[16];
			std::snprintf(hash, sizeof(hash), "-%08x", std::uint32_t(Death::Cryptography::xxHash3(lowercase.data(), lowercase.size())));
			String fileName = name + StringView(hash) + ".adj"_s;
			return fs::CombinePath({ ContentResolver::Get().GetCachePath(), "Editor"_s, "Adjacency"_s, StringView(fileName) });
		}

		// The 4 neighbors at the sides of a pattern of 8, up, right, down and left as bits 0 to 3
		std::uint8_t GetSides(std::uint8_t pattern)
		{
			return std::uint8_t(((pattern & SmartTileModel::Up) != 0 ? 1 : 0) | ((pattern & SmartTileModel::Right) != 0 ? 2 : 0) |
				((pattern & SmartTileModel::Down) != 0 ? 4 : 0) | ((pattern & SmartTileModel::Left) != 0 ? 8 : 0));
		}

		// Gives the reading of levels the number of tiles of the tile set, asked for before the thread starts, the library
		// of the editor isn't made for other threads
		class FixedTileSetLibrary : public ITileSetLibrary
		{
		public:
			FixedTileSetLibrary(StringView tileSet, std::int32_t tileCount)
				: _tileSet(tileSet), _tileCount(tileCount)
			{
			}

			std::int32_t GetTileCount(StringView name) override
			{
				return (EqualsIgnoreCase(name, _tileSet) ? _tileCount : -1);
			}

		private:
			String _tileSet;
			std::int32_t _tileCount;
		};

		// Bytes of the cache, little-endian whatever the platform is
		struct CacheWriter
		{
			SmallVector<std::uint8_t, 0> Data;

			void U8(std::uint8_t value) {
				Data.push_back(value);
			}
			void U32(std::uint32_t value) {
				for (std::int32_t i = 0; i < 4; i++) {
					Data.push_back(std::uint8_t(value >> (i * 8)));
				}
			}
			void U64(std::uint64_t value) {
				for (std::int32_t i = 0; i < 8; i++) {
					Data.push_back(std::uint8_t(value >> (i * 8)));
				}
			}
			void Text(StringView text) {
				U32(std::uint32_t(text.size()));
				for (char c : text) {
					Data.push_back(std::uint8_t(c));
				}
			}
		};

		// Reads the bytes of the cache, anything beyond the end fails, so a damaged file is only not used
		struct CacheReader
		{
			const std::uint8_t* Position;
			const std::uint8_t* End;

			std::size_t GetRemaining() const {
				return std::size_t(End - Position);
			}
			bool U8(std::uint8_t& value) {
				if (GetRemaining() < 1) {
					return false;
				}
				value = *Position++;
				return true;
			}
			bool U32(std::uint32_t& value) {
				if (GetRemaining() < 4) {
					return false;
				}
				value = 0;
				for (std::int32_t i = 0; i < 4; i++) {
					value |= std::uint32_t(Position[i]) << (i * 8);
				}
				Position += 4;
				return true;
			}
			bool U64(std::uint64_t& value) {
				if (GetRemaining() < 8) {
					return false;
				}
				value = 0;
				for (std::int32_t i = 0; i < 8; i++) {
					value |= std::uint64_t(Position[i]) << (i * 8);
				}
				Position += 8;
				return true;
			}
			bool Text(String& text) {
				std::uint32_t length;
				if (!U32(length) || length > MaxCachePath || GetRemaining() < length) {
					return false;
				}
				text = String(reinterpret_cast<const char*>(Position), length);
				Position += length;
				return true;
			}
		};

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

	}

	struct SmartTileModel::Job
	{
		// A level found, what it tells is kept in the cache by its path, its size and its time
		struct FileRecord
		{
			String Path;
			std::int64_t Size = 0;
			std::int64_t Time = 0;
			bool UsesTileSet = false;
			Contribution Data;
			// Of a record of the cache: the level is still there, and whether it's the same
			bool Seen = false;
			bool Reused = false;
		};

		// Set up before the job starts, only read afterwards
		String TileSet;
		std::int32_t TileCount = -1;
		std::int64_t TileSetSize = -1;
		std::int64_t TileSetTime = 0;
		SmallVector<String, 0> Roots;
		String CachePath;

		// Of the job alone
		SmallVector<FileRecord, 0> Cached;
		SmallVector<FileRecord, 0> Records;
		SmallVector<String, 0> Directories;
		SmallVector<String, 0> Files;
		std::size_t NextFile = 0;
		std::int32_t DirectoriesVisited = 0;
		std::int32_t Phase = 0;
		std::int32_t ReadSinceSave = 0;
		bool Changed = false;
		TimeStamp SavedAt;
		// The cache being read, a part at a time, see LoadCacheStep()
		std::int32_t CacheStage = 0;
		std::unique_ptr<Stream> CacheStream;
		SmallVector<std::uint8_t, 0> CacheData;
		std::size_t CacheFilled = 0;
		CacheReader CacheParser{ nullptr, nullptr };
		std::uint32_t CacheRecordsLeft = 0;

		// Shared by the thread and the editor, under the lock
#if defined(SMART_TILES_THREAD)
		Mutex Lock;
		Thread Worker;
		bool Started = false;
#endif
		std::atomic<bool> Cancelled{false};
		SmallVector<Contribution, 0> Outbox;
		float Progress = 0.0f;
		bool Done = false;

		~Job()
		{
#if defined(SMART_TILES_THREAD)
			// It stops after the level it reads, which can take a moment for a large one
			Cancelled = true;
			if (Started) {
				Worker.Join();
			}
#endif
		}

		void Enter()
		{
#if defined(SMART_TILES_THREAD)
			Lock.Lock();
#endif
		}

		void Leave()
		{
#if defined(SMART_TILES_THREAD)
			Lock.Unlock();
#endif
		}

		// Does some of the work, returns `true` once all of it is done
		bool Step(float budgetMs)
		{
			TimeStamp start = TimeStamp::now();
			while (!Cancelled.load(std::memory_order_relaxed)) {
				switch (Phase) {
					case 0: {
						// The cache first, a part at a time, it can be large
						if (!LoadCacheStep()) {
							break;
						}
						for (const String& root : Roots) {
							if (fs::DirectoryExists(root)) {
								Directories.push_back(root);
							}
						}
						Phase = 1;
						break;
					}
					case 1: {
						// The levels of the game, the content and the user content, a directory at a time
						if (Directories.empty()) {
							std::sort(Files.begin(), Files.end());
							Files.erase(std::unique(Files.begin(), Files.end()), Files.end());
							SavedAt = TimeStamp::now();
							Phase = 2;
							break;
						}
						String path = Death::move(Directories.back());
						Directories.pop_back();
						// A link back to a directory above would never end
						if (++DirectoriesVisited > MaxDirectories) {
							Directories.clear();
							break;
						}
						for (auto item : fs::Directory(path, fs::EnumerationOptions::SkipDirectories)) {
							if (StringUtils::lowercase(fs::GetExtension(item)) == "j2l"_s) {
								Files.push_back(item);
							}
						}
						for (auto item : fs::Directory(path, fs::EnumerationOptions::SkipFiles)) {
							Directories.push_back(item);
						}
						break;
					}
					case 2: {
						if (NextFile >= Files.size()) {
							bool removed = false;
							for (const FileRecord& record : Cached) {
								removed |= !record.Seen;
							}
							if (Changed || removed) {
								SaveCache(true);
							}
							Enter();
							Progress = 1.0f;
							Done = true;
							Leave();
							Phase = 3;
							return true;
						}
						ReadFile(Files[NextFile]);
						NextFile++;
						Enter();
						Progress = float(NextFile) / float(Files.size());
						Leave();
						if (Changed && (ReadSinceSave >= SaveEveryLevels || SavedAt.secondsSince() >= SaveInterval)) {
							SaveCache(false);
						}
						break;
					}
					default:
						return true;
				}
				if (start.millisecondsSince() >= budgetMs) {
					return false;
				}
			}
			return true;
		}

		void Publish(const Contribution& contribution)
		{
			Contribution copy = contribution;
			Enter();
			Outbox.push_back(Death::move(copy));
			Leave();
		}

		FileRecord* FindCached(StringView path)
		{
			auto it = std::lower_bound(Cached.begin(), Cached.end(), path, [](const FileRecord& record, StringView value) {
				return (StringView(record.Path) < value);
			});
			return (it != Cached.end() && StringView(it->Path) == path ? &*it : nullptr);
		}

		void ReadFile(const String& path)
		{
			std::int64_t size = fs::GetFileSize(path);
			std::int64_t time = fs::GetLastModificationTime(path).ToUnixMilliseconds();
			FileRecord* cached = FindCached(path);
			if (cached != nullptr) {
				cached->Seen = true;
				if (cached->Size == size && cached->Time == time) {
					// Known already, it doesn't have to be read
					cached->Reused = true;
					if (cached->UsesTileSet) {
						Publish(cached->Data);
					}
					return;
				}
			}

			Changed = true;
			FileRecord& record = Records.emplace_back();
			record.Path = path;
			record.Size = size;
			record.Time = time;
			record.UsesTileSet = UsesTileSet(path, TileSet);
			if (!record.UsesTileSet) {
				return;
			}
			ReadSinceSave++;
			auto s = fs::Open(path, FileAccess::Read);
			LevelDocument document;
			FixedTileSetLibrary library(TileSet, TileCount);
			if (s->IsValid() && document.Load(*s, &library)) {
				Collect(document, 1, record.Data);
				Publish(record.Data);
			} else {
				// A damaged level isn't read again until it changes
				record.UsesTileSet = false;
			}
		}

		// Reads a part of the cache, returns `true` once it's read whole, or it can't be, a damaged one isn't used at all
		bool LoadCacheStep()
		{
			auto finish = [this](bool damaged) {
				if (damaged) {
					LOGW("Cache of the tiles of \"{}\" is damaged, the levels are read again", TileSet);
					Cached.clear();
				}
				CacheStream = nullptr;
				CacheData = SmallVector<std::uint8_t, 0>();
				CacheParser = CacheReader{ nullptr, nullptr };
				CacheStage = 3;
				return true;
			};
			switch (CacheStage) {
				case 0: {
					CacheStream = fs::Open(CachePath, FileAccess::Read);
					std::int64_t size = (CacheStream->IsValid() ? CacheStream->GetSize() : 0);
					if (size < 8 || size > MaxCacheSize) {
						return finish(false);
					}
					CacheData.resize(std::size_t(size));
					CacheFilled = 0;
					CacheStage = 1;
					return false;
				}
				case 1: {
					std::size_t chunk = std::min<std::size_t>(CacheChunkBytes, CacheData.size() - CacheFilled);
					if (CacheStream->Read(CacheData.data() + CacheFilled, std::int64_t(chunk)) != std::int64_t(chunk)) {
						return finish(false);
					}
					CacheFilled += chunk;
					if (CacheFilled < CacheData.size()) {
						return false;
					}
					CacheStream = nullptr;
					CacheParser = CacheReader{ CacheData.data(), CacheData.data() + CacheData.size() };
					std::uint64_t magic;
					if (!CacheParser.U64(magic) || magic != CacheMagic) {
						// Another version, it's written anew
						return finish(false);
					}
					std::int32_t header = ReadCacheHeader(CacheParser);
					if (header <= 0) {
						// Damaged, or of the tile set as it was before it changed, all levels are read again
						return finish(header < 0);
					}
					CacheStage = 2;
					return false;
				}
				case 2: {
					const std::uint8_t* from = CacheParser.Position;
					while (CacheRecordsLeft > 0 && std::size_t(CacheParser.Position - from) < CacheChunkBytes) {
						if (!ReadCacheRecord(CacheParser)) {
							return finish(true);
						}
						CacheRecordsLeft--;
					}
					if (CacheRecordsLeft > 0) {
						return false;
					}
					if (CacheParser.GetRemaining() != 0) {
						return finish(true);
					}
					std::sort(Cached.begin(), Cached.end(), [](const FileRecord& a, const FileRecord& b) {
						return (StringView(a.Path) < StringView(b.Path));
					});
					return finish(false);
				}
				default:
					return true;
			}
		}

		// Returns 1 if the records of the cache follow, 0 if it's of another tile set or of one that changed since, -1 if
		// it's damaged
		std::int32_t ReadCacheHeader(CacheReader& reader)
		{
			String tileSet;
			std::uint64_t tileSetSize, tileSetTime;
			std::uint32_t tileCount, count;
			if (!reader.Text(tileSet) || !reader.U64(tileSetSize) || !reader.U64(tileSetTime) || !reader.U32(tileCount) || !reader.U32(count)) {
				return -1;
			}
			if (!EqualsIgnoreCase(tileSet, TileSet) || std::int64_t(tileSetSize) != TileSetSize || std::int64_t(tileSetTime) != TileSetTime ||
				std::int32_t(tileCount) != TileCount) {
				return 0;
			}
			// Every record takes some bytes at least, more of them than the file can hold is a damaged file
			if (count > MaxCacheRecords || std::size_t(count) > reader.GetRemaining() / 21) {
				return -1;
			}
			CacheRecordsLeft = count;
			Cached.reserve(count);
			return 1;
		}

		// Whether a tile of the counts of the cache is a tile of the tile set, its index and flips
		bool IsCachedTile(std::uint64_t tile) const
		{
			return (tile < (1u << 18) && (TileCount <= 0 || (tile & 0xFFFF) < std::uint64_t(TileCount)));
		}

		bool ReadCacheRecord(CacheReader& reader)
		{
			// The tiles of the counts are of the tile set, the patterns of 8 neighbors, anything else is a damaged file
			auto readCounts = [this, &reader](auto& list, bool wideKey) {
				std::uint32_t length;
				std::size_t entrySize = (wideKey ? 12 : 8);
				if (!reader.U32(length) || std::size_t(length) > reader.GetRemaining() / entrySize) {
					return false;
				}
				list.reserve(length);
				for (std::uint32_t i = 0; i < length; i++) {
					// Enough bytes are left for all of them, checked above
					std::uint64_t key = 0;
					std::uint32_t key32 = 0, value = 0;
					if (wideKey) {
						reader.U64(key);
						if (!IsCachedTile(key >> 32) || !IsCachedTile(key & 0xFFFFFFFFu)) {
							return false;
						}
					} else {
						reader.U32(key32);
						if ((key32 >> 26) != 0 || !IsCachedTile(key32 & 0x3FFFFu)) {
							return false;
						}
						key = key32;
					}
					reader.U32(value);
					list.push_back({ decltype(list[0].first)(key), value });
				}
				return true;
			};
			FileRecord& record = Cached.emplace_back();
			std::uint64_t size, time;
			std::uint8_t uses;
			if (!reader.Text(record.Path) || !reader.U64(size) || !reader.U64(time) || !reader.U8(uses)) {
				return false;
			}
			record.Size = std::int64_t(size);
			record.Time = std::int64_t(time);
			record.UsesTileSet = (uses != 0);
			return (!record.UsesTileSet || (readCounts(record.Data.Counts[0], false) && readCounts(record.Data.Counts[1], false) &&
				readCounts(record.Data.RightPairs, true) && readCounts(record.Data.DownPairs, true)));
		}

		// Partly written, the records of the cache not looked at yet stay, so they don't have to be read again either
		void SaveCache(bool final)
		{
			CacheWriter writer;
			writer.U64(CacheMagic);
			writer.Text(TileSet);
			writer.U64(std::uint64_t(TileSetSize));
			writer.U64(std::uint64_t(TileSetTime));
			writer.U32(std::uint32_t(TileCount));
			std::uint32_t count = std::uint32_t(Records.size());
			for (const FileRecord& record : Cached) {
				if (record.Reused || (!final && !record.Seen)) {
					count++;
				}
			}
			writer.U32(count);
			auto writeRecord = [&writer](const FileRecord& record) {
				writer.Text(record.Path);
				writer.U64(std::uint64_t(record.Size));
				writer.U64(std::uint64_t(record.Time));
				writer.U8(record.UsesTileSet ? 1 : 0);
				if (!record.UsesTileSet) {
					return;
				}
				for (const auto& counts : record.Data.Counts) {
					writer.U32(std::uint32_t(counts.size()));
					for (const auto& [key, value] : counts) {
						writer.U32(key);
						writer.U32(value);
					}
				}
				for (const auto* pairs : { &record.Data.RightPairs, &record.Data.DownPairs }) {
					writer.U32(std::uint32_t(pairs->size()));
					for (const auto& [key, value] : *pairs) {
						writer.U64(key);
						writer.U32(value);
					}
				}
			};
			for (const FileRecord& record : Cached) {
				if (record.Reused || (!final && !record.Seen)) {
					writeRecord(record);
				}
			}
			for (const FileRecord& record : Records) {
				writeRecord(record);
			}

			fs::CreateDirectories(fs::GetDirectoryName(CachePath));
			if (!AtomicFile::Write(CachePath, writer.Data.data(), std::int64_t(writer.Data.size()))) {
				LOGW("Cache of the tiles of \"{}\" cannot be written", TileSet);
			}
			ReadSinceSave = 0;
			SavedAt = TimeStamp::now();
		}
	};

	struct SmartTileModel::Collector
	{
		std::unordered_map<std::uint32_t, std::uint32_t> Counts[2];
		std::unordered_map<std::uint64_t, std::uint32_t> RightPairs;
		std::unordered_map<std::uint64_t, std::uint32_t> DownPairs;

		// Counts the tiles of rows of a layer, by the tiles around them, those above and below the rows too
		void AddRows(const DocumentLayer& layer, std::int32_t fromRow, std::int32_t toRow, std::uint32_t weight)
		{
			std::int32_t width = layer.GetWidth(), height = layer.GetHeight();
			if (layer.Tiles.size() < std::size_t(width) * std::size_t(height)) {
				return;
			}
			auto& layerCounts = Counts[layer.IsSpriteLayer() ? 1 : 0];
			for (std::int32_t y = std::max(fromRow, 0); y < std::min(toRow, height); y++) {
				for (std::int32_t x = 0; x < width; x++) {
					const TileRef& tile = layer.GetTile(x, y);
					if (!IsModelTile(tile)) {
						continue;
					}
					// Outside of the layer counts as not empty, so tiles at its edges don't look like they're in the open
					std::uint8_t pattern = 0;
					for (std::int32_t i = 0; i < 8; i++) {
						std::int32_t nx = x + ToolMath::NeighborOffsets[i][0], ny = y + ToolMath::NeighborOffsets[i][1];
						if (nx < 0 || ny < 0 || nx >= width || ny >= height || ToolMath::IsSolidTile(layer.GetTile(nx, ny))) {
							pattern |= std::uint8_t(1 << i);
						}
					}
					std::uint32_t key = ToKey(tile);
					layerCounts[(std::uint32_t(pattern) << 18) | key] += weight;

					if (x + 1 < width && IsModelTile(layer.GetTile(x + 1, y))) {
						RightPairs[(std::uint64_t(key) << 32) | ToKey(layer.GetTile(x + 1, y))] += weight;
					}
					if (y + 1 < height && IsModelTile(layer.GetTile(x, y + 1))) {
						DownPairs[(std::uint64_t(key) << 32) | ToKey(layer.GetTile(x, y + 1))] += weight;
					}
				}
			}
		}

		void Finish(Contribution& contribution)
		{
			for (std::int32_t kind = 0; kind < 2; kind++) {
				contribution.Counts[kind].reserve(Counts[kind].size());
				for (const auto& [key, count] : Counts[kind]) {
					contribution.Counts[kind].push_back({ key, count });
				}
			}
			contribution.RightPairs.reserve(RightPairs.size());
			for (const auto& [key, count] : RightPairs) {
				contribution.RightPairs.push_back({ key, count });
			}
			contribution.DownPairs.reserve(DownPairs.size());
			for (const auto& [key, count] : DownPairs) {
				contribution.DownPairs.push_back({ key, count });
			}
		}
	};

	SmartTileModel::SmartTileModel()
		: _requested(false), _ready(true), _library(nullptr), _progress(0.0f), _levelCount(0), _tileCount(-1), _revision(0),
			_documentTileBands(0), _documentLearned(false), _documentHasTiles(false), _documentKey(0), _documentPass(false),
			_documentPassLayer(0), _documentPassBand(0), _documentPassKey(0), _documentFrame(0), _documentFrameMs(0.0f),
			_patternDirty{}, _sidesDirty{}, _mostUsed{ UINT32_MAX, UINT32_MAX }, _mostUsedDirty{ true, true }, _neighborsDirty(false),
			_neighborsBuilt(false)
	{
	}

	SmartTileModel::~SmartTileModel() = default;
	SmartTileModel::SmartTileModel(SmartTileModel&& other) = default;
	SmartTileModel& SmartTileModel::operator=(SmartTileModel&& other) = default;

	void SmartTileModel::StopJob()
	{
		// Waits for the thread to stop
		_job = nullptr;
	}

	void SmartTileModel::Reset()
	{
		StopJob();
		_tileSet = {};
		_requested = false;
		_ready = true;
		_progress = 0.0f;
		_levelCount = 0;
		_tileCount = -1;
		_revision++;
		_rightPairs.clear();
		_downPairs.clear();
		_surfaceUsage.clear();
		for (std::int32_t kind = 0; kind < 2; kind++) {
			for (std::int32_t pattern = 0; pattern < 256; pattern++) {
				_counts[kind][pattern].clear();
				_byPattern[kind][pattern].clear();
				_patternDirty[kind][pattern] = false;
			}
			for (std::int32_t sides = 0; sides < 16; sides++) {
				_bySides[kind][sides].clear();
				_sidesDirty[kind][sides] = false;
			}
			_tileTotals[kind].clear();
			_mostUsed[kind] = UINT32_MAX;
			_mostUsedDirty[kind] = true;
		}
		for (auto& lists : _neighborsOf) {
			lists.clear();
		}
		_neighborsDirty = false;
		_neighborsBuilt = false;
		_documentBands.clear();
		_documentTileBands = 0;
		_documentLearned = false;
		_documentHasTiles = false;
		_documentKey = 0;
		_documentPass = false;
	}

	void SmartTileModel::Request(StringView tileSet, ITileSetLibrary& library)
	{
		// Called whenever the model is used, several times a frame, so nothing is allocated for it
		if (_requested && EqualsIgnoreCase(_tileSet, tileSet)) {
			return;
		}
		Reset();
		_requested = true;
		_tileSet = tileSet;
		_library = &library;
		_rules.Load(tileSet);
		if (tileSet.empty()) {
			return;
		}

		auto job = std::make_unique<Job>();
		job->TileSet = tileSet;
		job->TileCount = library.GetTileCount(tileSet);
		_tileCount = job->TileCount;
		// A changed tile set changes everything learned
		String tileSetPath = ContentTileSetLibrary::FindTileSetFile(tileSet);
		if (!tileSetPath.empty()) {
			job->TileSetSize = fs::GetFileSize(tileSetPath);
			job->TileSetTime = fs::GetLastModificationTime(tileSetPath).ToUnixMilliseconds();
		}
		auto& resolver = ContentResolver::Get();
		job->Roots.push_back(fs::CombinePath(resolver.GetCachePath(), "Episodes"_s));
		job->Roots.push_back(fs::CombinePath(resolver.GetContentPath(), "Episodes"_s));
		StringView userContent = resolver.GetUserContentPath();
		if (!userContent.empty()) {
			job->Roots.push_back(fs::CombinePath(userContent, "Episodes"_s));
		}
		job->CachePath = GetCachePath(tileSet);
		_job = Death::move(job);
		_ready = false;

#if defined(SMART_TILES_THREAD)
		_job->Worker = Thread([](void* arg) {
			Thread::SetCurrentName("Smart tiles");
			auto* job = static_cast<Job*>(arg);
			while (!job->Step(1.0e9f)) {
			}
		}, _job.get());
		_job->Started = true;
#endif
	}

	void SmartTileModel::Update()
	{
		if (_job == nullptr) {
			return;
		}
#if !defined(SMART_TILES_THREAD)
		_job->Step(StepBudgetMs);
#endif
		// What was read is taken in a little at a time, the levels known from the cache come all at once
		TimeStamp start = TimeStamp::now();
		bool done = false;
		while (true) {
			Contribution contribution;
			bool any = false;
			_job->Enter();
			if (!_job->Outbox.empty()) {
				contribution = Death::move(_job->Outbox.back());
				_job->Outbox.pop_back();
				any = true;
			}
			_progress = _job->Progress;
			done = (_job->Done && _job->Outbox.empty());
			_job->Leave();
			if (!any) {
				break;
			}
			Merge(contribution, false);
			_levelCount++;
			if (start.millisecondsSince() >= MergeBudgetMs) {
				break;
			}
		}
		if (done) {
			LOGI("Tiles of \"{}\" learned, as {} levels use them", _tileSet, _levelCount);
			StopJob();
			_ready = true;
			_progress = 1.0f;
		}
	}

	float SmartTileModel::GetProgress() const
	{
		return (_ready ? 1.0f : _progress);
	}

	bool SmartTileModel::CanPick() const
	{
		return (_levelCount > 0 || _documentHasTiles || _rules.GetCount() > 0);
	}

	void SmartTileModel::LearnDocument(const LevelDocument& document, std::uint32_t revision)
	{
		// The revision, and the layers themselves for another level with the same revision
		std::uint64_t key = 0;
		auto mix = [&key](std::uint64_t value) {
			key = Death::Cryptography::xxHash3(&value, sizeof(value), key);
		};
		mix(revision);
		mix(std::uint64_t(reinterpret_cast<std::uintptr_t>(&document)));
		for (const DocumentLayer& layer : document.Layers) {
			mix(std::uint64_t(reinterpret_cast<std::uintptr_t>(layer.Tiles.data())));
			mix((std::uint64_t(std::uint32_t(layer.GetWidth())) << 32) | std::uint32_t(layer.GetHeight()));
		}
		if (!_documentPass) {
			if (_documentLearned && (key == _documentKey || _documentLearnedAt.secondsSince() < DocumentRelearnInterval)) {
				return;
			}
			// The level as it is now, a change while it's gone through is learned the next time
			_documentPass = true;
			_documentPassLayer = 0;
			_documentPassBand = 0;
			_documentPassKey = key;
		}

		// A little of it each frame, the model is used many times a frame
		std::uint32_t frame = theApplication().GetFrameCount();
		if (frame != _documentFrame) {
			_documentFrame = frame;
			_documentFrameMs = 0.0f;
		}
		if (_documentFrameMs >= DocumentBudgetMs) {
			return;
		}
		TimeStamp start = TimeStamp::now();
		while (true) {
			if (_documentPassLayer >= document.Layers.size()) {
				// What layers removed since told is forgotten
				while (_documentBands.size() > document.Layers.size()) {
					for (DocumentBand& band : _documentBands.back()) {
						ForgetBand(band);
					}
					_documentBands.pop_back();
				}
				_documentPass = false;
				_documentLearned = true;
				_documentKey = _documentPassKey;
				_documentLearnedAt = TimeStamp::now();
				break;
			}
			const DocumentLayer& layer = document.Layers[_documentPassLayer];
			if (_documentBands.size() <= _documentPassLayer) {
				_documentBands.resize(_documentPassLayer + 1);
			}
			SmallVector<DocumentBand, 0>& bands = _documentBands[_documentPassLayer];
			std::int32_t width = layer.GetWidth(), height = layer.GetHeight();
			bool valid = (width > 0 && height > 0 && layer.Tiles.size() >= std::size_t(width) * std::size_t(height));
			std::int32_t bandCount = (valid ? (height + DocumentBandRows - 1) / DocumentBandRows : 0);
			if (_documentPassBand == 0) {
				// As many bands as the layer has rows for now, what those of rows gone told is forgotten
				while (std::int32_t(bands.size()) > bandCount) {
					ForgetBand(bands.back());
					bands.pop_back();
				}
				bands.resize(std::size_t(bandCount));
			}
			if (_documentPassBand >= bandCount || _documentPassBand >= std::int32_t(bands.size())) {
				_documentPassLayer++;
				_documentPassBand = 0;
				continue;
			}
			LearnBand(layer, _documentPassBand, bands[_documentPassBand]);
			_documentPassBand++;
			if (_documentFrameMs + start.millisecondsSince() >= DocumentBudgetMs) {
				break;
			}
		}
		_documentFrameMs += start.millisecondsSince();
	}

	void SmartTileModel::LearnBand(const DocumentLayer& layer, std::int32_t band, DocumentBand& state)
	{
		// The rows of the band and those next to them, the neighbors of its tiles, by what tells anything of them
		std::int32_t width = layer.GetWidth(), height = layer.GetHeight();
		std::int32_t fromRow = band * DocumentBandRows, toRow = std::min(fromRow + DocumentBandRows, height);
		std::int32_t hashFrom = std::max(fromRow - 1, 0), hashTo = std::min(toRow + 1, height);
		const std::int32_t header[4] = { width, height, fromRow, layer.IsSpriteLayer() ? 1 : 0 };
		std::uint64_t hash = Death::Cryptography::xxHash3(header, sizeof(header));
		hash = Death::Cryptography::xxHash3(&layer.Tiles[std::size_t(hashFrom) * std::size_t(width)],
			std::size_t(hashTo - hashFrom) * std::size_t(width) * sizeof(TileRef), hash);
		if (state.Learned && state.Hash == hash) {
			return;
		}

		ForgetBand(state);
		Collector collector;
		collector.AddRows(layer, fromRow, toRow, DocumentWeight);
		collector.Finish(state.Data);
		Merge(state.Data, false);
		if (!state.Data.Counts[0].empty() || !state.Data.Counts[1].empty()) {
			_documentTileBands++;
		}
		_documentHasTiles = (_documentTileBands > 0);
		state.Hash = hash;
		state.Learned = true;
	}

	void SmartTileModel::ForgetBand(DocumentBand& state)
	{
		if (!state.Learned) {
			return;
		}
		Merge(state.Data, true);
		if (!state.Data.Counts[0].empty() || !state.Data.Counts[1].empty()) {
			_documentTileBands--;
		}
		_documentHasTiles = (_documentTileBands > 0);
		state.Data = Contribution();
		state.Learned = false;
	}

	void SmartTileModel::Collect(const LevelDocument& document, std::uint32_t weight, Contribution& contribution)
	{
		Collector collector;
		for (const DocumentLayer& layer : document.Layers) {
			collector.AddRows(layer, 0, layer.GetHeight(), weight);
		}
		collector.Finish(contribution);
	}

	bool SmartTileModel::IsKnownTileKey(std::uint32_t tile) const
	{
		// A level saved with a larger tile set, imported again with fewer tiles since, has tiles past its end
		return (tile < (1u << 18) && (_tileCount <= 0 || (tile & 0xFFFF) < std::uint32_t(_tileCount)));
	}

	void SmartTileModel::Merge(const Contribution& contribution, bool subtract)
	{
		auto apply = [subtract](auto& map, auto key, std::uint32_t count) {
			if (!subtract) {
				map[key] += count;
				return;
			}
			auto it = map.find(key);
			if (it != map.end()) {
				if (it->second <= count) {
					map.erase(it);
				} else {
					it->second -= count;
				}
			}
		};
		for (std::int32_t kind = 0; kind < 2; kind++) {
			for (const auto& [key, count] : contribution.Counts[kind]) {
				std::uint32_t tile = (key & 0x3FFFF);
				std::uint8_t pattern = std::uint8_t(key >> 18);
				if ((key >> 26) != 0 || !IsKnownTileKey(tile)) {
					continue;
				}
				apply(_counts[kind][pattern], tile, count);
				apply(_tileTotals[kind], tile, count);
				if ((pattern & Up) == 0 && (pattern & Down) != 0) {
					apply(_surfaceUsage, tile, count);
				}
				_patternDirty[kind][pattern] = true;
				_sidesDirty[kind][GetSides(pattern)] = true;
			}
			if (!contribution.Counts[kind].empty()) {
				_mostUsedDirty[kind] = true;
			}
		}
		for (std::int32_t side = 0; side < 2; side++) {
			auto& pairs = (side == 0 ? _rightPairs : _downPairs);
			for (const auto& [key, count] : (side == 0 ? contribution.RightPairs : contribution.DownPairs)) {
				if (!IsKnownTileKey(std::uint32_t(key >> 32)) || !IsKnownTileKey(std::uint32_t(key))) {
					continue;
				}
				apply(pairs, key, count);
				_neighborsDirty = true;
			}
		}
		_revision++;
	}

	const SmallVector<SmartTileModel::Candidate, 0>& SmartTileModel::GetByPattern(std::int32_t kind, std::uint8_t pattern)
	{
		SmallVector<Candidate, 0>& list = _byPattern[kind][pattern];
		if (_patternDirty[kind][pattern]) {
			_patternDirty[kind][pattern] = false;
			list.clear();
			list.reserve(_counts[kind][pattern].size());
			for (const auto& [tile, count] : _counts[kind][pattern]) {
				list.push_back(Candidate{ tile, count });
			}
			std::sort(list.begin(), list.end(), [](const Candidate& a, const Candidate& b) { return (a.Count != b.Count ? a.Count > b.Count : a.Tile < b.Tile); });
		}
		return list;
	}

	const SmallVector<SmartTileModel::Candidate, 0>& SmartTileModel::GetBySides(std::int32_t kind, std::uint8_t sides)
	{
		SmallVector<Candidate, 0>& list = _bySides[kind][sides];
		if (_sidesDirty[kind][sides]) {
			_sidesDirty[kind][sides] = false;
			// The 16 patterns with these sides, whatever the corners are
			std::unordered_map<std::uint32_t, std::uint32_t> totals;
			for (std::int32_t pattern = 0; pattern < 256; pattern++) {
				if (GetSides(std::uint8_t(pattern)) != sides) {
					continue;
				}
				for (const auto& [tile, count] : _counts[kind][pattern]) {
					totals[tile] += count;
				}
			}
			list.clear();
			list.reserve(totals.size());
			for (const auto& [tile, count] : totals) {
				list.push_back(Candidate{ tile, count });
			}
			std::sort(list.begin(), list.end(), [](const Candidate& a, const Candidate& b) { return (a.Count != b.Count ? a.Count > b.Count : a.Tile < b.Tile); });
		}
		return list;
	}

	std::uint32_t SmartTileModel::GetMostUsed(std::int32_t kind)
	{
		if (_mostUsedDirty[kind]) {
			_mostUsedDirty[kind] = false;
			_mostUsed[kind] = UINT32_MAX;
			std::uint32_t mostUsedCount = 0;
			for (const auto& [tile, count] : _tileTotals[kind]) {
				if (count > mostUsedCount || (count == mostUsedCount && tile < _mostUsed[kind])) {
					mostUsedCount = count;
					_mostUsed[kind] = tile;
				}
			}
		}
		return _mostUsed[kind];
	}

	void SmartTileModel::AddLevel(const LevelDocument& document, std::uint32_t weight)
	{
		Contribution contribution;
		Collect(document, weight, contribution);
		Merge(contribution, false);
		_levelCount++;
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

		// Those of the layer kind first, then of the others, then with the neighbors at the sides only
		std::int32_t kind = (spriteLayer ? 1 : 0);
		std::uint8_t sides = GetSides(neighbors);
		const SmallVector<Candidate, 0>* candidates = &GetByPattern(kind, neighbors);
		if (candidates->empty()) {
			candidates = &GetByPattern(1 - kind, neighbors);
		}
		if (candidates->empty()) {
			candidates = &GetBySides(kind, sides);
		}
		if (candidates->empty()) {
			candidates = &GetBySides(1 - kind, sides);
		}
		if (candidates->empty()) {
			// Nothing was learned with neighbors like these, any tile is better than one that only stands for solid ground
			std::uint32_t mostUsed = GetMostUsed(kind);
			if (mostUsed == UINT32_MAX) {
				mostUsed = GetMostUsed(1 - kind);
			}
			if (mostUsed == UINT32_MAX) {
				return false;
			}
			result = FromKey(mostUsed);
			return true;
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
		// The key doesn't tell the tile sets apart, a tile of another one isn't known at all
		if (!IsModelTile(tile)) {
			return 0;
		}
		auto it = _surfaceUsage.find(ToKey(tile));
		return (it != _surfaceUsage.end() ? it->second : 0);
	}

	std::uint32_t SmartTileModel::GetCount(std::uint8_t neighbors, bool spriteLayer, const TileRef& tile) const
	{
		if (!IsModelTile(tile)) {
			return 0;
		}
		std::uint32_t key = ToKey(tile);
		const auto& counts = _counts[spriteLayer ? 1 : 0][neighbors];
		auto it = counts.find(key);
		std::uint32_t count = (it != counts.end() ? it->second : 0);
		// The other layers count much less, they have tiles in front of or behind the ground
		const auto& otherCounts = _counts[spriteLayer ? 0 : 1][neighbors];
		auto other = otherCounts.find(key);
		return count + (other != otherCounts.end() ? other->second / 16 : 0);
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
		EnsureNeighbors();
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
		for (std::int32_t i = 0; i < 256; i++) {
			if (TerrainRules::Reduce(std::uint8_t(i)) != reduced) {
				continue;
			}
			for (const auto& [tile, count] : _counts[spriteLayer ? 1 : 0][i]) {
				totals[tile] += count;
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

	void SmartTileModel::GetCoverage(bool spriteLayer, std::uint8_t (&coverage)[256]) const
	{
		// As Pick() finds tiles for the neighbors of the pattern: by a rule, or by what was learned of these very neighbors
		// on either kind of layers; only the sides of them, which it falls back to, aren't enough
		std::memset(coverage, 0, sizeof(coverage));
		std::int32_t kind = (spriteLayer ? 1 : 0);
		for (std::int32_t pattern = 0; pattern < 256; pattern++) {
			if (TerrainRules::Reduce(std::uint8_t(pattern)) != pattern) {
				continue;
			}
			if (_rules.Find(std::uint8_t(pattern)) != nullptr) {
				coverage[pattern] = 2;
			} else if (!_counts[kind][pattern].empty() || !_counts[1 - kind][pattern].empty()) {
				coverage[pattern] = 1;
			}
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

	void SmartTileModel::EnsureNeighbors()
	{
		// All pairs are gone through, which takes a while with many levels, so not more often than once in a while
		if (!_neighborsDirty || (_neighborsBuilt && _neighborsBuiltAt.secondsSince() < RebuildInterval)) {
			return;
		}
		_neighborsDirty = false;
		_neighborsBuilt = true;
		_neighborsBuiltAt = TimeStamp::now();

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
