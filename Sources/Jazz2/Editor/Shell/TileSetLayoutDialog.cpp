#include "TileSetLayoutDialog.h"

#if defined(WITH_EDITOR)

#include "ProgressDialog.h"
#include "TextPrompt.h"
#include "TileSetsDialog.h"
#include "../Tools/EditorContext.h"
#include "../Common/TileStamp.h"
#include "../Document/CommandHistory.h"
#include "../Document/LevelDocument.h"
#include "../Document/Commands.h"
#include "../Document/DocumentSnapshot.h"
#include "../Files/AtomicFile.h"
#include "../Files/ContentTileSetLibrary.h"
#include "../Files/JsonAccess.h"
#include "../Imaging/TileImages.h"
#include "../Runtime/TileDrawing.h"
#include "../Smart/RulesFile.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiSurface.h"
#include "../../ContentResolver.h"
#include "../../Formats/LevelFile.h"
#include "../../Formats/TileSetFile.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		// Tiles in a row of the grid, as tile sets are drawn
		constexpr std::int32_t Columns = 10;
		// Size of a cell of the grid in units, the window makes it as large as fits up to the size of a tile
		constexpr float MinCellUnits = 16.0f;
		constexpr float MaxCellUnits = 32.0f;

		// Where a level that uses the tile set has its tiles, read from the start of its file
		struct LevelUse
		{
			String Path;
			bool Writable = false;
			// The tile set is its default one
			bool Default = false;
			String DefaultTileSet;
			// Extra tile sets of the level: the first tile taken and how many, and whether it's the tile set, in order
			struct Extra
			{
				std::uint16_t Offset;
				std::uint16_t Count;
				bool IsTileSet;
			};
			SmallVector<Extra, 0> Extras;
		};

		struct LayoutState
		{
			String TileSet;
			std::int32_t OriginalCount = 0;
			// The old position of the tile at each new position, -1 for a new empty tile
			SmallVector<std::int32_t, 0> Order;
			// Whether the tile at each position is selected, the empty tile first never is
			SmallVector<bool, 0> Selected;
			// Where a Shift+click selects from
			std::int32_t Anchor = -1;
			// The old tile each old tile was merged into as a duplicate, -1 for each that wasn't
			SmallVector<std::int32_t, 0> MergedInto;
			// Other levels that use the tile set, as the default one or an extra one, those in places that can be
			// written to and the others
			SmallVector<String, 0> WritableLevels;
			SmallVector<String, 0> ReadOnlyLevels;
			SmallVector<LevelUse, 0> Uses;

			void SyncSelection() {
				Selected.resize(Order.size(), false);
				if (!Selected.empty()) {
					Selected[0] = false;
				}
			}
			void SelectOnly(std::int32_t index) {
				Selected.assign(Order.size(), false);
				if (index >= 1 && index < std::int32_t(Order.size())) {
					Selected[index] = true;
				}
				Anchor = index;
			}
			std::int32_t GetSelectedCount() const {
				std::int32_t count = 0;
				for (std::size_t i = 0; i < Selected.size() && i < Order.size(); i++) {
					count += (Selected[i] ? 1 : 0);
				}
				return count;
			}
			bool HasSelection() const {
				return (GetSelectedCount() > 0);
			}
			bool IsChanged() const {
				if (std::int32_t(Order.size()) != OriginalCount) {
					return true;
				}
				for (std::int32_t i = 0; i < std::int32_t(Order.size()); i++) {
					if (Order[i] != i) {
						return true;
					}
				}
				for (std::int32_t merged : MergedInto) {
					if (merged >= 0) {
						return true;
					}
				}
				return false;
			}
			void Reset() {
				Order.clear();
				for (std::int32_t i = 0; i < OriginalCount; i++) {
					Order.push_back(i);
				}
				MergedInto.assign(std::size_t(OriginalCount), -1);
				SelectOnly(-1);
			}
		};

		void CollectFiles(StringView path, StringView extension, bool recursive, SmallVectorImpl<String>& files)
		{
			if (!fs::DirectoryExists(path)) {
				return;
			}
			for (auto item : fs::Directory(path, fs::EnumerationOptions::SkipDirectories)) {
				if (StringUtils::lowercase(fs::GetExtension(item)) == extension) {
					files.push_back(item);
				}
			}
			if (recursive) {
				for (auto item : fs::Directory(path, fs::EnumerationOptions::SkipFiles)) {
					CollectFiles(item, extension, true, files);
				}
			}
		}

		// A path as it's compared, the same file found from two places must be changed only once
		String NormalizePath(StringView path)
		{
			return StringUtils::lowercase(fs::FromNativeSeparators(path));
		}

		// Names of tile sets are compared as the file system finds them
		bool IsSameTileSet(StringView a, StringView b)
		{
			return (!a.empty() && NormalizePath(a) == NormalizePath(b));
		}

		// How a level uses a tile set, as its default one or an extra one, read only from the start of the file,
		// returns `false` if it doesn't
		bool ReadLevelUse(StringView path, StringView tileSet, LevelUse& use)
		{
			auto s = fs::Open(path, FileAccess::Read);
			Formats::LevelHeader header;
			if (!s->IsValid() || !Formats::LevelFile::ReadHeader(*s, header)) {
				return false;
			}
			Formats::LevelReader reader(*s, header);
			Formats::LevelProperties properties;
			reader.ReadProperties(properties);
			if (!reader.IsValid()) {
				return false;
			}
			use.DefaultTileSet = properties.DefaultTileSet;
			use.Default = IsSameTileSet(properties.DefaultTileSet, tileSet);
			reader.ReadPalettes(nullptr, nullptr);
			SmallVector<Formats::ExtraTileSetRecord, 0> extraTileSets;
			reader.ReadExtraTileSets(extraTileSets);
			if (!reader.IsValid()) {
				return use.Default;
			}
			bool any = use.Default;
			for (const Formats::ExtraTileSetRecord& record : extraTileSets) {
				bool isTileSet = ((record.Flags & Formats::ExtraTileSetRecord::Embedded) == 0 && IsSameTileSet(record.Name, tileSet));
				use.Extras.push_back({ record.Offset, record.Count, isTileSet });
				any |= isTileSet;
			}
			return any;
		}

		// What looking for the levels that use the tile set needs from the editor, taken on the main thread
		struct ScanInput
		{
			String TileSet;
			SmallVector<String, 4> Roots;
			String OwnPath;
			String OwnDirectory;
			SmallVector<String, 4> WritableRoots;
		};

		ScanInput PrepareScan(IEditorContext& session, StringView tileSet)
		{
			ScanInput input;
			input.TileSet = tileSet;
			auto& resolver = ContentResolver::Get();
			input.Roots.push_back(fs::CombinePath(resolver.GetCachePath(), "Episodes"_s));
			input.Roots.push_back(fs::CombinePath(resolver.GetContentPath(), "Episodes"_s));
			StringView userContent = resolver.GetUserContentPath();
			if (!userContent.empty()) {
				input.Roots.push_back(fs::CombinePath(userContent, "Episodes"_s));
			}
			// The level being edited may be anywhere, the levels next to it are likely of the same episode
			input.OwnPath = session.GetFilePath();
			if (!input.OwnPath.empty()) {
				input.OwnDirectory = fs::GetDirectoryName(input.OwnPath);
			}
			SmallVector<UiFileRoot, 4> roots;
			session.GetFileRoots(true, roots);
			for (const UiFileRoot& root : roots) {
				if (root.Writable) {
					input.WritableRoots.push_back(NormalizePath(root.Path));
				}
			}
			if (!input.OwnDirectory.empty() && fs::IsWritable(input.OwnDirectory)) {
				input.WritableRoots.push_back(NormalizePath(input.OwnDirectory));
			}
			return input;
		}

		// Finds the levels that use the tile set, on any thread, it reads only files
		void ScanLevels(const ScanInput& input, SmallVectorImpl<LevelUse>& uses, JobProgress* progress)
		{
			SmallVector<String, 0> files;
			for (const String& root : input.Roots) {
				CollectFiles(root, "j2l"_s, true, files);
			}
			if (!input.OwnDirectory.empty()) {
				CollectFiles(input.OwnDirectory, "j2l"_s, false, files);
			}

			// Each file once, however it was found, otherwise it would be changed twice
			SmallVector<std::pair<String, String>, 0> unique;
			unique.reserve(files.size());
			for (String& file : files) {
				unique.emplace_back(NormalizePath(file), Death::move(file));
			}
			std::sort(unique.begin(), unique.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
			unique.erase(std::unique(unique.begin(), unique.end(), [](const auto& a, const auto& b) { return a.first == b.first; }), unique.end());

			String ownKey = NormalizePath(input.OwnPath);
			for (std::size_t i = 0; i < unique.size(); i++) {
				if (progress != nullptr && !progress->Report(float(i) / float(unique.size()))) {
					return;
				}
				const auto& [key, file] = unique[i];
				LevelUse use;
				if (!ReadLevelUse(file, input.TileSet, use)) {
					continue;
				}
				use.Path = file;
				for (const String& root : input.WritableRoots) {
					if (IsPathInside(key, root)) {
						use.Writable = true;
						break;
					}
				}
				if (!use.Writable && key == ownKey) {
					// The level being edited follows the change in any case, it's saved elsewhere then
					continue;
				}
				uses.push_back(Death::move(use));
			}
		}

		void BuildTileSet(const Formats::TileSetData& original, ArrayView<const std::int32_t> order, Formats::TileSetData& result)
		{
			result = Formats::TileSetData();
			std::memcpy(result.Palette, original.Palette, sizeof(result.Palette));
			result.Resize(std::int32_t(order.size()));
			for (std::int32_t i = 0; i < std::int32_t(order.size()); i++) {
				std::int32_t from = order[i];
				if (from < 0 || from >= original.TileCount) {
					continue;
				}
				std::memcpy(&result.Images[std::size_t(i) * Formats::TileSetData::ImageBytesPerTile],
					&original.Images[std::size_t(from) * Formats::TileSetData::ImageBytesPerTile], Formats::TileSetData::ImageBytesPerTile);
				std::memcpy(&result.Masks[std::size_t(i) * Formats::TileSetData::MaskBytesPerTile],
					&original.Masks[std::size_t(from) * Formats::TileSetData::MaskBytesPerTile], Formats::TileSetData::MaskBytesPerTile);
				result.TrueColor[i] = original.TrueColor[from];
			}
		}

		// Numbers of tiles of tile sets as they were taken on the main thread, levels are loaded with them off it, and
		// with the number a tile set had before it changed
		class FixedTileCounts : public ITileSetLibrary
		{
		public:
			void Set(StringView name, std::int32_t count)
			{
				for (auto& [known, knownCount] : _counts) {
					if (IsSameTileSet(known, name)) {
						knownCount = count;
						return;
					}
				}
				_counts.emplace_back(String(name), count);
			}

			bool Contains(StringView name) const
			{
				for (const auto& [known, count] : _counts) {
					if (IsSameTileSet(known, name)) {
						return true;
					}
				}
				return false;
			}

			std::int32_t GetTileCount(StringView name) override
			{
				for (const auto& [known, count] : _counts) {
					if (IsSameTileSet(known, name)) {
						return count;
					}
				}
				return -1;
			}

		private:
			SmallVector<std::pair<String, std::int32_t>, 0> _counts;
		};

		// A level file changed to the new order of its tile set, only into its temporary file, see AtomicFile::Stage(),
		// on any thread, it reads only files and @p tileSets
		bool StageLevelFile(ITileSetLibrary& tileSets, StringView path, StringView tileSet, ArrayView<const std::int32_t> order, ArrayView<const std::int32_t> mergedInto)
		{
			LevelDocument document;
			{
				auto s = fs::Open(path, FileAccess::Read);
				if (!s->IsValid() || !document.Load(*s, &tileSets)) {
					return false;
				}
			}
			if (RemapLevelToTileSetOrder(document, order, tileSet, mergedInto) < 0) {
				return false;
			}
			return AtomicFile::Stage(path, [&document](Stream& s) {
				return document.Save(s, false, 9);
			});
		}

		// Files written next to their targets, replaced together only once all of them could be written
		struct StagedFile
		{
			String Path;
			bool Existed;
		};

		void DiscardStaged(ArrayView<const StagedFile> staged)
		{
			for (const StagedFile& file : staged) {
				AtomicFile::Discard(file.Path);
			}
		}

		// Replaces all files by those staged, the previous ones kept as .bak; if one can't be replaced, those replaced
		// already are put back and the rest discarded, returns the index of the one that failed, or -1
		std::int32_t CommitStaged(ArrayView<const StagedFile> staged)
		{
			std::size_t committed = 0;
			while (committed < staged.size() && AtomicFile::Commit(staged[committed].Path, true)) {
				committed++;
			}
			if (committed == staged.size()) {
				return -1;
			}
			for (std::size_t i = 0; i < committed; i++) {
				if (!AtomicFile::RestoreBackup(staged[i].Path, staged[i].Existed)) {
					LOGE("File \"{}\" couldn't be put back, its previous content is in \"{}.bak\"", staged[i].Path, staged[i].Path);
				}
			}
			for (std::size_t i = committed; i < staged.size(); i++) {
				AtomicFile::Discard(staged[i].Path);
			}
			return std::int32_t(committed);
		}

		/**
		 * The tiles the rules of a tile set keep of tile set @p tileSet follow its new order, the removed ones are
		 * left out. The rules of the tile set itself refer to it without a name, and have its slope tags too.
		 */
		bool RemapRules(Json::Value& doc, bool ownTileSet, StringView tileSet, ArrayView<const std::int32_t> newIndex)
		{
			if (!doc.isObject()) {
				return false;
			}
			auto isTarget = [ownTileSet, tileSet](const Json::Value& item, const char* key) {
				String name = JsonAccess::GetString(item, key);
				return (name.empty() ? ownTileSet : IsSameTileSet(name, tileSet));
			};
			auto remapIndex = [newIndex](std::uint32_t index) {
				return (index < newIndex.size() ? newIndex[index] : -1);
			};

			bool changed = false;
			if (doc.isMember("terrains") && doc["terrains"].isArray()) {
				for (Json::Value& terrain : doc["terrains"]) {
					if (!terrain.isObject() || !terrain.isMember("patterns") || !terrain["patterns"].isObject()) {
						continue;
					}
					Json::Value& patterns = terrain["patterns"];
					for (const std::string& name : patterns.getMemberNames()) {
						Json::Value& list = patterns[name];
						if (!list.isArray()) {
							continue;
						}
						Json::Value kept(Json::arrayValue);
						for (Json::Value& item : list) {
							if (item.isObject() && isTarget(item, "s")) {
								std::uint32_t index = JsonAccess::GetUInt(item, "i", 0);
								std::int32_t newPosition = remapIndex(index);
								if (newPosition <= 0) {
									changed = true;
									continue;
								}
								if (std::uint32_t(newPosition) != index) {
									item["i"] = newPosition;
									changed = true;
								}
							}
							kept.append(item);
						}
						list = Death::move(kept);
					}
				}
			}

			// The tiles of a prefab, of its own layer and of the other layers it has
			auto remapTiles = [&](Json::Value& tiles) {
				if (!tiles.isArray()) {
					return;
				}
				for (Json::Value& tile : tiles) {
					if (!tile.isObject() || !isTarget(tile, "set")) {
						continue;
					}
					std::uint32_t index = JsonAccess::GetUInt(tile, "i", 0);
					std::int32_t newPosition = remapIndex(index);
					if (newPosition <= 0) {
						// Not painted anymore, as a removed tile isn't in the level either
						tile = Json::Value(Json::nullValue);
						changed = true;
					} else if (std::uint32_t(newPosition) != index) {
						tile["i"] = newPosition;
						changed = true;
					}
				}
			};
			if (doc.isMember("prefabs") && doc["prefabs"].isArray()) {
				for (Json::Value& prefab : doc["prefabs"]) {
					if (!prefab.isObject()) {
						continue;
					}
					if (prefab.isMember("tiles")) {
						remapTiles(prefab["tiles"]);
					}
					if (prefab.isMember("layers") && prefab["layers"].isArray()) {
						for (Json::Value& layer : prefab["layers"]) {
							if (layer.isObject() && layer.isMember("tiles")) {
								remapTiles(layer["tiles"]);
							}
						}
					}
				}
			}

			if (ownTileSet && doc.isMember("slopes") && doc["slopes"].isObject()) {
				Json::Value& slopes = doc["slopes"];
				Json::Value moved(Json::objectValue);
				for (const std::string& name : slopes.getMemberNames()) {
					std::int32_t index = std::atoi(name.c_str());
					std::int32_t newPosition = (index > 0 ? remapIndex(std::uint32_t(index)) : -1);
					if (newPosition != index) {
						changed = true;
					}
					if (newPosition > 0) {
						moved[std::to_string(newPosition)] = slopes[name];
					}
				}
				slopes = Death::move(moved);
			}
			return changed;
		}

		// Names of all tile sets that have rules, as the rules are looked for
		void CollectRulesNames(SmallVectorImpl<String>& names)
		{
			auto& resolver = ContentResolver::Get();
			SmallVector<String, 4> roots;
			StringView userContent = resolver.GetUserContentPath();
			if (!userContent.empty()) {
				roots.push_back(fs::CombinePath(userContent, "Tilesets"_s));
			}
			roots.push_back(fs::CombinePath(resolver.GetContentPath(), "Tilesets"_s));
			roots.push_back(fs::CombinePath(resolver.GetCachePath(), "Tilesets"_s));
			for (const String& root : roots) {
				SmallVector<String, 0> files;
				CollectFiles(root, "rules"_s, true, files);
				for (const String& file : files) {
					StringView relative = StringView(file).exceptPrefix(std::min(root.size(), file.size()));
					while (!relative.empty() && (relative[0] == '/' || relative[0] == '\\')) {
						relative = relative.exceptPrefix(1);
					}
					String name = fs::FromNativeSeparators(relative.exceptSuffix(std::min<std::size_t>(relative.size(), 6)));
					bool found = false;
					for (const String& other : names) {
						if (IsSameTileSet(other, name)) {
							found = true;
							break;
						}
					}
					if (!found && !name.empty()) {
						names.push_back(Death::move(name));
					}
				}
			}
		}

		// Whether the rules of tile set @p name refer to any tile of tile set @p tileSet
		bool RulesReferTo(const Json::Value& doc, StringView name, StringView tileSet)
		{
			// Every tile is gone by an empty new order, so anything that refers to one would change
			Json::Value copy = doc;
			return RemapRules(copy, IsSameTileSet(name, tileSet), tileSet, {});
		}

		/**
		 * Prefabs, terrain rules and slope tags of all tile sets follow the new order of the tile set, written only into
		 * their temporary files, they're committed with the other files. Returns `false` if the rules of the tile set
		 * itself can't be read or any can't be written, @p failed is the tile set of those then.
		 */
		bool StageAllRules(StringView tileSet, ArrayView<const std::int32_t> newIndex, SmallVectorImpl<StagedFile>& staged, String& failed)
		{
			SmallVector<String, 0> names;
			CollectRulesNames(names);
			for (const String& name : names) {
				Json::Value doc;
				RulesFile::ReadResult result = RulesFile::TryRead(name, doc);
				if (result == RulesFile::ReadResult::Unreadable && IsSameTileSet(name, tileSet)) {
					// Its prefabs would refer to other tiles once the file is fixed
					failed = name;
					return false;
				}
				if (result != RulesFile::ReadResult::Ok || !RemapRules(doc, IsSameTileSet(name, tileSet), tileSet, newIndex)) {
					continue;
				}
				String path = RulesFile::GetWritablePath(name, true);
				bool existed = (!path.empty() && fs::FileExists(path));
				if (!RulesFile::Stage(name, doc, path)) {
					failed = name;
					return false;
				}
				staged.push_back({ path, existed });
			}
			return true;
		}

		// The new position of each old tile, -1 for those that are gone; a tile merged into another as its duplicate
		// goes where that one goes
		void MakeNewIndex(ArrayView<const std::int32_t> order, ArrayView<const std::int32_t> mergedInto, SmallVectorImpl<std::int32_t>& newIndex)
		{
			newIndex.clear();
			for (std::int32_t i = 0; i < std::int32_t(order.size()); i++) {
				if (order[i] >= 0) {
					if (std::size_t(order[i]) >= newIndex.size()) {
						newIndex.resize(std::size_t(order[i]) + 1, -1);
					}
					newIndex[order[i]] = i;
				}
			}
			if (newIndex.size() < mergedInto.size()) {
				newIndex.resize(mergedInto.size(), -1);
			}
			for (std::size_t i = 0; i < mergedInto.size(); i++) {
				std::int32_t target = mergedInto[i];
				if (newIndex[i] < 0 && target >= 0 && std::size_t(target) < newIndex.size()) {
					newIndex[i] = newIndex[target];
				}
			}
		}

		// Where the tiles of a tile set go in a level that uses it, as its default tile set and as extra ones
		struct OrderRemap
		{
			SmallVector<std::int32_t, 0> NewIndex;
			bool Default = false;
			// The first tile and the number of tiles of each extra tile set that is the tile set, before and after,
			// -1 for the other ones
			SmallVector<std::int32_t, 0> OldOffset;
			SmallVector<std::int32_t, 0> NewOffset;
			SmallVector<std::int32_t, 0> NewCount;

			bool Remap(TileRef& tile) const
			{
				if (tile.Source == TileRef::DefaultTileSet) {
					if (!Default) {
						return true;
					}
					std::int32_t index = (tile.Index < NewIndex.size() ? NewIndex[tile.Index] : -1);
					if (index < 0) {
						return false;
					}
					tile.Index = std::uint16_t(index);
					return true;
				}
				if (tile.Source > TileRef::LastExtraTileSet) {
					return true;
				}
				std::size_t slot = std::size_t(tile.Source) - 1;
				if (slot >= OldOffset.size() || OldOffset[slot] < 0) {
					return true;
				}
				std::int32_t from = std::int32_t(tile.Index) + OldOffset[slot];
				std::int32_t index = (from < std::int32_t(NewIndex.size()) ? NewIndex[from] : -1);
				if (index < NewOffset[slot] || index >= NewOffset[slot] + NewCount[slot]) {
					return false;
				}
				tile.Index = std::uint16_t(index - NewOffset[slot]);
				return true;
			}
		};

		std::int32_t RemapLevel(LevelDocument& document, ArrayView<const std::int32_t> order, ArrayView<const std::int32_t> mergedInto,
			StringView tileSet, bool extraTileSets, OrderRemap& plan)
		{
			if (order.empty()) {
				return -1;
			}
			// Without a name, it's the default tile set, whatever it's called
			plan.Default = (tileSet.empty() || IsSameTileSet(document.Properties.DefaultTileSet, tileSet));
			if (tileSet.empty()) {
				tileSet = document.Properties.DefaultTileSet;
			}
			MakeNewIndex(order, mergedInto, plan.NewIndex);
			std::size_t slotCount = std::min<std::size_t>(document.ExtraTileSets.size(), TileRef::LastExtraTileSet);
			plan.OldOffset.assign(slotCount, -1);
			plan.NewOffset.assign(slotCount, -1);
			plan.NewCount.assign(slotCount, -1);
			bool any = plan.Default;
			std::int32_t newTotal = std::int32_t(order.size());
			for (std::size_t slot = 0; extraTileSets && slot < slotCount; slot++) {
				const Formats::ExtraTileSetRecord& record = document.ExtraTileSets[slot];
				if ((record.Flags & Formats::ExtraTileSetRecord::Embedded) != 0 || !IsSameTileSet(record.Name, tileSet)) {
					continue;
				}
				any = true;
				// The part of the tile set it takes becomes the part its tiles are in now
				std::int32_t first = INT32_MAX, last = -1;
				for (std::int32_t from = record.Offset; from < std::int32_t(record.Offset) + std::int32_t(record.Count); from++) {
					std::int32_t index = (from < std::int32_t(plan.NewIndex.size()) ? plan.NewIndex[from] : -1);
					if (index >= 0) {
						first = std::min(first, index);
						last = std::max(last, index);
					}
				}
				plan.OldOffset[slot] = record.Offset;
				if (last < 0) {
					// None of its tiles is left, it keeps a single tile so the record stays valid
					plan.NewOffset[slot] = std::clamp<std::int32_t>(record.Offset, 0, std::max<std::int32_t>(newTotal - 1, 0));
					plan.NewCount[slot] = 1;
				} else {
					plan.NewOffset[slot] = first;
					plan.NewCount[slot] = std::min<std::int32_t>(last - first + 1, UINT16_MAX);
				}
			}
			if (!any) {
				return -1;
			}

			// Overrides are kept by tile ID, they're found by their tiles before the tile sets change
			LevelDocument::OverrideRefs overrides = document.CaptureOverrideRefs();
			if (plan.Default) {
				document.DefaultTileCount = newTotal;
				// The tile shown with the name of the level is a tile of the default tile set too
				std::uint16_t caption = document.Properties.CaptionTileId;
				if (caption != 0) {
					std::int32_t index = (caption < plan.NewIndex.size() ? plan.NewIndex[caption] : -1);
					document.Properties.CaptionTileId = std::uint16_t(std::max<std::int32_t>(index, 0));
				}
			}
			for (std::size_t slot = 0; slot < slotCount; slot++) {
				if (plan.OldOffset[slot] >= 0) {
					document.ExtraTileSets[slot].Offset = std::uint16_t(plan.NewOffset[slot]);
					document.ExtraTileSets[slot].Count = std::uint16_t(plan.NewCount[slot]);
				}
			}
			Function<bool(TileRef&)> remap = [&plan](TileRef& tile) {
				return plan.Remap(tile);
			};
			std::int32_t cleared = document.RemapTiles(overrides, remap);
			// Tiles merged as duplicates may have both been changed in the level, the first one kept stays
			auto dropSameIds = [](auto& list) {
				list.erase(std::unique(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.TileId == b.TileId; }), list.end());
			};
			dropSameIds(document.DiffuseOverrides);
			dropSameIds(document.MaskOverrides);
			document.FitTileIdSpace();
			return cleared;
		}

		/**
		 * The level being edited follows the new order as one step, with the brush. With @p newTileSet, its default tile
		 * set is replaced by the new one in the new order, its extra tile sets stay as they are.
		 */
		void ApplyToDocument(IEditorContext& session, ArrayView<const std::int32_t> order, ArrayView<const std::int32_t> mergedInto, StringView tileSet,
			StringView newTileSet, StringView name)
		{
			// The properties have the tile shown with the name of the level, it follows the order too
			SnapshotParts parts = SnapshotParts::TileSets | SnapshotParts::Animations | SnapshotParts::Layers | SnapshotParts::Properties;
			std::int32_t cleared = 0;
			session.RecordSnapshotStep(name, parts, [&](LevelDocument& document) {
				OrderRemap plan;
				cleared = RemapLevel(document, order, mergedInto, tileSet, newTileSet.empty(), plan);
				if (!newTileSet.empty()) {
					document.Properties.DefaultTileSet = newTileSet;
				}

				TileStamp stamp = session.GetStamp();
				for (TileRef& tile : stamp.Tiles) {
					if (!tile.IsEmpty() && cleared >= 0 && !plan.Remap(tile)) {
						tile = TileRef{};
					}
				}
				session.SetStamp(Death::move(stamp));
				return true;
			});
			if (cleared > 0) {
				session.ShowMessage(_fnx("editor", "{} tile that isn't in the tile set anymore was cleared", "{} tiles that aren't in the tile set anymore were cleared", cleared, cleared));
			}
		}

		// Moves the selected tiles, in their order, before a position, they stay selected
		void MoveSelected(LayoutState& state, std::int32_t before)
		{
			state.SyncSelection();
			SmallVector<std::int32_t, 0> moved, rest;
			std::int32_t insertAt = before;
			for (std::int32_t i = 0; i < std::int32_t(state.Order.size()); i++) {
				if (state.Selected[i]) {
					moved.push_back(state.Order[i]);
					if (i < before) {
						insertAt--;
					}
				} else {
					rest.push_back(state.Order[i]);
				}
			}
			if (moved.empty()) {
				return;
			}
			insertAt = std::clamp<std::int32_t>(insertAt, 1, std::int32_t(rest.size()));
			rest.insert(rest.begin() + insertAt, moved.begin(), moved.end());
			state.Order = Death::move(rest);
			state.Selected.assign(state.Order.size(), false);
			for (std::int32_t i = 0; i < std::int32_t(moved.size()); i++) {
				state.Selected[insertAt + i] = true;
			}
		}

		// Moves each selected tile a place earlier or later, past the tile that isn't selected next to it
		void MoveSelectedBy(LayoutState& state, std::int32_t direction)
		{
			state.SyncSelection();
			auto& order = state.Order;
			auto& selected = state.Selected;
			std::int32_t count = std::int32_t(order.size());
			if (direction < 0) {
				for (std::int32_t i = 2; i < count; i++) {
					if (selected[i] && !selected[i - 1]) {
						std::swap(order[i - 1], order[i]);
						selected[i - 1] = true;
						selected[i] = false;
					}
				}
			} else {
				for (std::int32_t i = count - 2; i >= 1; i--) {
					if (selected[i] && !selected[i + 1]) {
						std::swap(order[i + 1], order[i]);
						selected[i + 1] = true;
						selected[i] = false;
					}
				}
			}
		}

		void DeleteSelected(LayoutState& state)
		{
			state.SyncSelection();
			SmallVector<std::int32_t, 0> kept;
			std::int32_t first = -1;
			for (std::int32_t i = 0; i < std::int32_t(state.Order.size()); i++) {
				if (state.Selected[i]) {
					if (first < 0) {
						first = i;
					}
				} else {
					kept.push_back(state.Order[i]);
				}
			}
			state.Order = Death::move(kept);
			state.SelectOnly(std::min<std::int32_t>(first, std::int32_t(state.Order.size()) - 1));
		}

		void InsertEmptyBeforeSelected(LayoutState& state)
		{
			state.SyncSelection();
			for (std::int32_t i = 1; i < std::int32_t(state.Order.size()); i++) {
				if (state.Selected[i]) {
					state.Order.insert(state.Order.begin() + i, -1);
					state.Selected.insert(state.Selected.begin() + i, false);
					return;
				}
			}
		}

		// Leaves out the tiles that look the same as one before them, with the same mask, the levels then show that one;
		// returns how many were left out
		std::int32_t MergeDuplicates(LayoutState& state, const Formats::TileSetData& data)
		{
			auto bytesOf = [&data](std::int32_t tile) {
				return (data.TrueColor[tile] != 0 ? std::size_t(Formats::TileSetData::ImageBytesPerTile) : std::size_t(32 * 32));
			};
			auto hashOf = [&data](std::int32_t tile) {
				return HashTileOf(data, tile);
			};
			auto same = [&data, &bytesOf](std::int32_t a, std::int32_t b) {
				return (data.TrueColor[a] == data.TrueColor[b] &&
					std::memcmp(&data.Images[std::size_t(a) * Formats::TileSetData::ImageBytesPerTile], &data.Images[std::size_t(b) * Formats::TileSetData::ImageBytesPerTile], bytesOf(a)) == 0 &&
					std::memcmp(&data.Masks[std::size_t(a) * Formats::TileSetData::MaskBytesPerTile], &data.Masks[std::size_t(b) * Formats::TileSetData::MaskBytesPerTile], Formats::TileSetData::MaskBytesPerTile) == 0);
			};

			state.MergedInto.resize(std::size_t(state.OriginalCount), -1);
			std::unordered_multimap<std::uint64_t, std::int32_t> kept;
			SmallVector<std::int32_t, 0> order;
			std::int32_t merged = 0;
			for (std::int32_t i = 0; i < std::int32_t(state.Order.size()); i++) {
				std::int32_t tile = state.Order[i];
				if (tile < 0 || tile >= data.TileCount) {
					order.push_back(tile);
					continue;
				}
				std::uint64_t hash = hashOf(tile);
				std::int32_t into = -1;
				auto range = kept.equal_range(hash);
				for (auto it = range.first; it != range.second && into < 0; ++it) {
					if (same(it->second, tile)) {
						into = it->second;
					}
				}
				// The empty tile first is never left out, a tile like it goes into it
				if (into >= 0 && i > 0) {
					state.MergedInto[tile] = into;
					merged++;
					continue;
				}
				kept.emplace(hash, tile);
				order.push_back(tile);
			}
			state.Order = Death::move(order);
			state.SelectOnly(-1);
			return merged;
		}

		// Marks the tiles of the tile set a level shows, read from its file. Its extra tile sets come after the tiles
		// of its default tile set, @p defaultCount of them, or -1 if it isn't known.
		void MarkLevelUsage(const LevelUse& use, StringView tileSet, std::int32_t defaultCount, SmallVectorImpl<bool>& used)
		{
			auto mark = [&used](std::int32_t index) {
				if (index >= 0 && index < std::int32_t(used.size())) {
					used[index] = true;
				}
			};
			auto s = fs::Open(use.Path, FileAccess::Read);
			Formats::LevelHeader header;
			if (!s->IsValid() || !Formats::LevelFile::ReadHeader(*s, header)) {
				return;
			}
			Formats::LevelReader reader(*s, header);
			Formats::LevelProperties properties;
			reader.ReadProperties(properties);
			reader.ReadPalettes(nullptr, nullptr);
			SmallVector<Formats::ExtraTileSetRecord, 0> extraTileSets;
			reader.ReadExtraTileSets(extraTileSets);
			reader.ReadTileDiffuseOverrides(nullptr);
			reader.ReadTileMaskOverrides(nullptr);
			SmallVector<String, 0> texts;
			reader.ReadTexts(texts);
			std::uint16_t animatedOffset = 0;
			SmallVector<Formats::AnimatedTileRecord, 0> animatedTiles;
			reader.ReadAnimatedTiles(animatedOffset, animatedTiles);
			if (!reader.IsValid()) {
				return;
			}

			// The parts of the tile set its extra tile sets take, and where their tile IDs start
			struct Window
			{
				std::int32_t Base, Offset, Count;
			};
			SmallVector<Window, 4> windows;
			std::int32_t base = defaultCount;
			for (const Formats::ExtraTileSetRecord& record : extraTileSets) {
				if ((record.Flags & Formats::ExtraTileSetRecord::Embedded) == 0 && IsSameTileSet(record.Name, tileSet)) {
					if (base < 0) {
						// Where its tiles are can't be told, all of them count as used
						for (std::int32_t i = 0; i < record.Count; i++) {
							mark(record.Offset + i);
						}
					} else {
						windows.push_back({ base, record.Offset, record.Count });
					}
				}
				if (base >= 0) {
					base += record.Count;
				}
			}
			bool isDefault = IsSameTileSet(properties.DefaultTileSet, tileSet);
			if (isDefault && properties.CaptionTileId != 0) {
				// The tile shown with the name of the level
				mark(properties.CaptionTileId);
			}
			auto markId = [&](std::int32_t id) {
				if (id >= animatedOffset) {
					return;
				}
				if (isDefault && (defaultCount < 0 || id < defaultCount)) {
					mark(id);
					return;
				}
				for (const Window& window : windows) {
					if (id >= window.Base && id < window.Base + window.Count) {
						mark(window.Offset + id - window.Base);
						return;
					}
				}
			};
			for (const Formats::AnimatedTileRecord& animation : animatedTiles) {
				for (const Formats::TileRecord& frame : animation.Frames) {
					markId(frame.TileId);
				}
			}
			std::int32_t layerCount = reader.ReadLayerCount();
			for (std::int32_t i = 0; i < layerCount && reader.IsValid(); i++) {
				Formats::LayerRecord layer;
				reader.ReadLayerHeader(layer);
				if (!reader.IsValid() || layer.Width < 0 || layer.Height < 0 || std::int64_t(layer.Width) * layer.Height > Formats::LevelFile::MaxLayerTiles) {
					return;
				}
				reader.ReadLayerTiles(layer, [&markId](std::int32_t, ArrayView<const Formats::TileRecord> tiles) {
					for (const Formats::TileRecord& tile : tiles) {
						if (tile.TileId != 0) {
							markId(tile.TileId);
						}
					}
				});
			}
		}

		template<class Mark>
		void MarkRulesUsage(const Json::Value& rules, bool ownTileSet, StringView tileSet, Mark& mark);

		// Marks the tiles of the tile set the level being edited shows and its brush has, and those the prefabs and rules
		// of all tile sets keep
		void MarkOwnUsage(IEditorContext& session, StringView tileSet, SmallVectorImpl<bool>& used)
		{
			auto mark = [&used](std::int32_t index) {
				if (index >= 0 && index < std::int32_t(used.size())) {
					used[index] = true;
				}
			};
			const LevelDocument& document = session.GetDocument();
			bool isDefault = IsSameTileSet(document.Properties.DefaultTileSet, tileSet);
			auto markRef = [&](const TileRef& tile) {
				if (tile.IsEmpty() || tile.IsAnimated() || tile.Source == TileRef::Raw) {
					return;
				}
				if (tile.Source == TileRef::DefaultTileSet) {
					if (isDefault) {
						mark(tile.Index);
					}
					return;
				}
				std::size_t slot = std::size_t(tile.Source) - 1;
				if (slot < document.ExtraTileSets.size()) {
					const Formats::ExtraTileSetRecord& record = document.ExtraTileSets[slot];
					if ((record.Flags & Formats::ExtraTileSetRecord::Embedded) == 0 && IsSameTileSet(record.Name, tileSet)) {
						mark(tile.Index + record.Offset);
					}
				}
			};
			for (const DocumentLayer& layer : document.Layers) {
				for (const TileRef& tile : layer.Tiles) {
					markRef(tile);
				}
			}
			for (const DocumentAnimation& animation : document.Animations) {
				for (const TileRef& frame : animation.Frames) {
					markRef(frame);
				}
			}
			// The brush paints the tiles it has, and the tile shown with the name of the level is one of the default tile set
			for (const TileRef& tile : session.GetStamp().Tiles) {
				markRef(tile);
			}
			if (isDefault && document.Properties.CaptionTileId != 0) {
				mark(document.Properties.CaptionTileId);
			}
			// The prefabs, terrain rules and slope tags refer to tiles by their positions too, they'd lose them otherwise,
			// those of the other tile sets too, which may have tiles of this one
			SmallVector<String, 0> names;
			CollectRulesNames(names);
			for (const String& name : names) {
				Json::Value rules;
				if (RulesFile::Read(name, rules)) {
					MarkRulesUsage(rules, IsSameTileSet(name, tileSet), tileSet, mark);
				}
			}
		}

		// Marks the tiles of tile set @p tileSet the rules of a tile set keep, the rules of the tile set itself refer to it
		// without a name and have its slope tags too, as RemapRules() changes them
		template<class Mark>
		void MarkRulesUsage(const Json::Value& rules, bool ownTileSet, StringView tileSet, Mark& mark)
		{
			if (rules.isObject()) {
				auto isOwn = [ownTileSet, tileSet](const Json::Value& item, const char* key) {
					String name = JsonAccess::GetString(item, key);
					return (name.empty() ? ownTileSet : IsSameTileSet(name, tileSet));
				};
				const Json::Value& terrains = JsonAccess::Member(rules, "terrains");
				if (terrains.isArray()) {
					for (const Json::Value& terrain : terrains) {
						const Json::Value& patterns = JsonAccess::Member(terrain, "patterns");
						if (!patterns.isObject()) {
							continue;
						}
						for (const std::string& name : patterns.getMemberNames()) {
							const Json::Value& list = patterns[name];
							if (!list.isArray()) {
								continue;
							}
							for (const Json::Value& item : list) {
								if (item.isObject() && isOwn(item, "s")) {
									mark(std::int32_t(JsonAccess::GetUInt(item, "i", 0)));
								}
							}
						}
					}
				}
				// The tiles of a prefab, of its own layer and of the other layers it has
				auto markTiles = [&](const Json::Value& tiles) {
					if (!tiles.isArray()) {
						return;
					}
					for (const Json::Value& tile : tiles) {
						if (tile.isObject() && isOwn(tile, "set")) {
							mark(std::int32_t(JsonAccess::GetUInt(tile, "i", 0)));
						}
					}
				};
				const Json::Value& prefabs = JsonAccess::Member(rules, "prefabs");
				if (prefabs.isArray()) {
					for (const Json::Value& prefab : prefabs) {
						markTiles(JsonAccess::Member(prefab, "tiles"));
						const Json::Value& layers = JsonAccess::Member(prefab, "layers");
						if (layers.isArray()) {
							for (const Json::Value& layer : layers) {
								markTiles(JsonAccess::Member(layer, "tiles"));
							}
						}
					}
				}
				const Json::Value& slopes = JsonAccess::Member(rules, "slopes");
				if (ownTileSet && slopes.isObject()) {
					for (const std::string& name : slopes.getMemberNames()) {
						mark(std::atoi(name.c_str()));
					}
				}
			}
		}

		// Leaves out the tiles no level, prefab or rule uses, a tile merged into another counts for that one; returns how
		// many were left out
		std::int32_t RemoveUnused(LayoutState& state, SmallVectorImpl<bool>& used)
		{
			for (std::size_t i = 0; i < state.MergedInto.size() && i < used.size(); i++) {
				if (used[i] && state.MergedInto[i] >= 0 && std::size_t(state.MergedInto[i]) < used.size()) {
					used[state.MergedInto[i]] = true;
				}
			}
			SmallVector<std::int32_t, 0> order;
			std::int32_t removed = 0;
			for (std::int32_t i = 0; i < std::int32_t(state.Order.size()); i++) {
				std::int32_t tile = state.Order[i];
				// The empty tile and new empty ones stay where they were put
				if (i == 0 || tile < 0 || (tile < std::int32_t(used.size()) && used[tile])) {
					order.push_back(tile);
				} else {
					removed++;
				}
			}
			state.Order = Death::move(order);
			state.SelectOnly(-1);
			return removed;
		}

		/** The tiles in their new order, a click selects, with Ctrl one more, with Shift a range, they can be dragged */
		class LayoutGrid : public UiElement
		{
		public:
			// Size of a cell in units
			float CellSize = MinCellUnits;

			LayoutGrid(IEditorContext& session, std::shared_ptr<LayoutState> state)
				: _session(session), _state(Death::move(state))
			{
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				float cell = context.Px(CellSize);
				std::int32_t rows = (std::int32_t(_state->Order.size()) + Columns - 1) / Columns;
				return Vector2f(cell * Columns, cell * rows);
			}

			void Arrange(const UiContext& context, const Rectf& bounds) override
			{
				UiElement::Arrange(context, bounds);
				_cell = context.Px(CellSize);
			}

			void Draw(UiPainter& painter) override
			{
				Tiles::TileMap* tileMap = _session.GetTileMap();
				LevelDocument& document = _session.GetDocument();
				float thickness = std::max(1.0f, std::round(painter.GetScale()));
				// Only the rows in view, a tile set can have thousands of tiles
				std::int32_t count = std::int32_t(_state->Order.size());
				std::int32_t first = 0, last = count;
				const Rectf& clip = painter.GetClip();
				if (clip.W > 0.0f && clip.H > 0.0f && _cell > 0.0f) {
					first = std::max<std::int32_t>(0, std::int32_t((clip.Y - Bounds.Y) / _cell)) * Columns;
					last = std::min<std::int32_t>(count, (std::int32_t((clip.Y + clip.H - Bounds.Y) / _cell) + 1) * Columns);
				}
				for (std::int32_t i = first; i < last; i++) {
					Rectf rect = GetCellRect(i);
					std::int32_t from = _state->Order[i];
					bool moved = (from != i);
					painter.FillRect(Rectf(rect.X + 1.0f, rect.Y + 1.0f, rect.W - 2.0f, rect.H - 2.0f),
						from < 0 ? Colorf(0.2f, 0.4f, 0.25f, 0.5f) : Colorf(0.0f, 0.0f, 0.0f, 0.3f), UiLayer::Fill);
					if (from > 0 && tileMap != nullptr) {
						DrawTile(painter, *tileMap, document, TileRef{ std::uint16_t(from), TileRef::DefaultTileSet, 0 }, 0,
							Rectf(rect.X + 1.0f, rect.Y + 1.0f, rect.W - 2.0f, rect.H - 2.0f), 1.0f, UiLayer::Content);
					}
					if (moved && from >= 0) {
						// A dot in the corner of a tile that isn't where it was
						painter.FillRect(Rectf(rect.X + 2.0f, rect.Y + 2.0f, thickness * 3.0f, thickness * 3.0f), UiTheme::Accent, UiLayer::Overlay);
					}
					if (i < std::int32_t(_state->Selected.size()) && _state->Selected[i]) {
						painter.Frame(rect, UiTheme::Accent, thickness, UiLayer::Overlay);
					}
				}
				// Where the dragged tiles would go, before the tile under the pointer
				if (_dragging && _dropIndex >= 1) {
					Rectf rect = GetCellRect(std::min<std::int32_t>(_dropIndex, std::int32_t(_state->Order.size()) - 1));
					float x = (_dropIndex < std::int32_t(_state->Order.size()) ? rect.X : rect.X + rect.W);
					painter.FillRect(Rectf(x - thickness, rect.Y, thickness * 2.0f, rect.H), UiTheme::Accent, UiLayer::Overlay);
				}
			}

			bool AcceptsPointer() const override
			{
				return true;
			}

			bool OnPointerDown(const UiPointerEvent& event) override
			{
				std::int32_t index = GetIndexAt(event.Pos);
				if (index < 1) {
					// The empty tile stays first
					return false;
				}
				_state->SyncSelection();
				_dragging = false;
				_clickedIndex = -1;
				if ((event.Modifiers & KeyMod::Shift) != 0 && _state->Anchor >= 1) {
					// A range from the tile clicked before, added to what is selected with Ctrl
					if ((event.Modifiers & KeyMod::Ctrl) == 0) {
						_state->Selected.assign(_state->Order.size(), false);
					}
					for (std::int32_t i = std::min(_state->Anchor, index); i <= std::max(_state->Anchor, index); i++) {
						_state->Selected[i] = true;
					}
					return false;
				}
				if ((event.Modifiers & KeyMod::Ctrl) != 0) {
					// Tiles that aren't next to each other are picked one by one
					_state->Selected[index] = !_state->Selected[index];
					_state->Anchor = index;
					return false;
				}
				if (!_state->Selected[index]) {
					_state->SelectOnly(index);
				} else {
					// A click on a selected tile without dragging selects only it, when the button is released
					_clickedIndex = index;
					_state->Anchor = index;
				}
				// The selected tiles can be dragged to another place
				_dragStart = event.Pos;
				return true;
			}

			void OnPointerMove(const UiPointerEvent& event) override
			{
				if (!_pressed) {
					return;
				}
				if (!_dragging) {
					Vector2f delta = event.Pos - _dragStart;
					if (std::abs(delta.X) < _cell * 0.3f && std::abs(delta.Y) < _cell * 0.3f) {
						return;
					}
					_dragging = true;
				}
				_dropIndex = GetDropIndexAt(event.Pos);
			}

			void OnPointerUp(const UiPointerEvent& event, bool inside) override
			{
				if (_dragging) {
					_dragging = false;
					if (_dropIndex >= 1) {
						MoveSelected(*_state, _dropIndex);
					}
					_dropIndex = -1;
				} else if (_clickedIndex >= 1) {
					_state->SelectOnly(_clickedIndex);
				}
				_clickedIndex = -1;
			}

			StringView GetTooltip(Vector2f pos) const override
			{
				std::int32_t index = GetIndexAt(pos);
				if (index < 0) {
					return {};
				}
				std::int32_t from = _state->Order[index];
				// TRANSLATORS: Tooltips of a tile in the Rearrange Tile Set dialog, the first {} is its new place, "tile {} before" is the place it had
				_tooltip = (from < 0 ? String(_fx("editor", "{}: a new empty tile", index)) : (from == index ? String(_fx("editor", "Tile {}", index)) : String(_fx("editor", "{}: tile {} before", index, from))));
				return _tooltip;
			}

		private:
			IEditorContext& _session;
			std::shared_ptr<LayoutState> _state;
			float _cell = 0.0f;
			mutable String _tooltip;
			bool _dragging = false;
			Vector2f _dragStart;
			std::int32_t _dropIndex = -1;
			std::int32_t _clickedIndex = -1;

			Rectf GetCellRect(std::int32_t index) const
			{
				return Rectf(Bounds.X + (index % Columns) * _cell, Bounds.Y + (index / Columns) * _cell, _cell, _cell);
			}

			// The position the dragged tiles would be put before, the nearer edge of the cell under the pointer, after
			// the last tile past it, never before the empty tile
			std::int32_t GetDropIndexAt(Vector2f pos) const
			{
				if (_cell <= 0.0f) {
					return -1;
				}
				std::int32_t x = std::clamp<std::int32_t>(std::int32_t(std::round((pos.X - Bounds.X) / _cell)), 0, Columns);
				std::int32_t y = std::max<std::int32_t>(std::int32_t((pos.Y - Bounds.Y) / _cell), 0);
				std::int32_t index = y * Columns + x;
				return std::clamp<std::int32_t>(index, 1, std::int32_t(_state->Order.size()));
			}

			std::int32_t GetIndexAt(Vector2f pos) const
			{
				if (!Bounds.Contains(pos) || _cell <= 0.0f) {
					return -1;
				}
				std::int32_t x = std::int32_t((pos.X - Bounds.X) / _cell), y = std::int32_t((pos.Y - Bounds.Y) / _cell);
				std::int32_t index = y * Columns + x;
				return (x >= 0 && x < Columns && index >= 0 && index < std::int32_t(_state->Order.size()) ? index : -1);
			}
		};

		/** The window of the new order, the grid of the tiles takes what the window can grow by */
		class LayoutWindow : public UiDialog
		{
			DEATH_RUNTIME_OBJECT(UiDialog);

		public:
			LayoutGrid* Grid = nullptr;
			UiScrollView* GridScroll = nullptr;
			UiElement* GridColumn = nullptr;

			explicit LayoutWindow(StringView title)
				: UiDialog(title, true)
			{
				MaxScreenHeightFraction = 0.9f;
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				float scale = std::max(context.Scale, 0.5f);
				Vector2f units(available.X / scale, available.Y * MaxScreenHeightFraction / scale);
				if (Grid != nullptr && GridScroll != nullptr) {
					// Next to the controls, which take about 340 units with the padding, the cells as large as fit up to
					// the size of a tile; the grid is always 10 tiles wide, as tile sets are drawn
					float gridUnits = std::min(units.X * 0.5f, units.X - 370.0f);
					Grid->CellSize = std::floor(std::clamp(gridUnits / float(Columns), MinCellUnits, MaxCellUnits));
					float gridWidth = std::ceil(context.Px(Grid->CellSize) * float(Columns) / scale);
					GridScroll->Width = gridWidth + UiTheme::ScrollBarWidth + 1.0f;
					// As tall as the window may be without the title, the buttons, the text above the grid and the padding
					GridScroll->Height = std::floor(std::clamp(units.Y - 130.0f, 160.0f, 1200.0f));
					if (GridColumn != nullptr) {
						GridColumn->Width = GridScroll->Width;
					}
				}
				return UiDialog::Measure(context, available);
			}
		};
	}

	std::int32_t RemapLevelToTileSetOrder(LevelDocument& document, ArrayView<const std::int32_t> order, StringView tileSet, ArrayView<const std::int32_t> mergedInto)
	{
		OrderRemap plan;
		return RemapLevel(document, order, mergedInto, tileSet, true, plan);
	}

	namespace
	{
		// A line of a list of files, a header or a file
		struct FileLine
		{
			String Text;
			bool Header;
		};

		// Shows a list of files, with a button that does something with them if it's given
		void ShowFileList(IEditorContext& session, StringView title, SmallVector<FileLine, 0>&& lines, StringView action, Function<void()>&& onAction)
		{
			UiSurface* surface = session.GetSurface();
			if (surface == nullptr) {
				return;
			}
			auto dialog = std::make_unique<UiDialog>(title);
			UiDialog* dialogPtr = dialog.get();
			auto* scroll = dialog->GetContent()->Add<UiScrollView>();
			scroll->Width = 420.0f;
			scroll->Height = 240.0f;
			auto* list = scroll->SetContent<UiStack>(UiOrientation::Vertical, 2.0f);
			for (const FileLine& line : lines) {
				list->Add<UiLabel>(line.Text, line.Header ? UiTheme::TextHeader : UiTheme::TextDim);
			}
			if (onAction) {
				auto shared = std::make_shared<Function<void()>>(Death::move(onAction));
				dialog->AddButton(action, [shared, dialogPtr]() {
					dialogPtr->Close();
					(*shared)();
				}, true);
				dialog->AddButton(_x("editor", "Cancel"), [dialogPtr]() { dialogPtr->Close(); });
			}
			// Only a list to read otherwise, closed by its cross or Escape
			ShowModal(*surface, Death::move(dialog));
		}

		// The files changing the tile set would write, nothing is written
		void ShowChangedFiles(IEditorContext& session, const LayoutState& state)
		{
			SmallVector<FileLine, 0> lines;
			String tileSetPath = ContentTileSetLibrary::GetWritablePath(state.TileSet, false);
			// TRANSLATORS: Header in the list of the files changing a tile set would write, followed by the file of the tile set
			lines.push_back({ _x("editor", "Tile set"), true });
			// TRANSLATORS: After the path of the tile set file in the list of the files changing it would write, what happens to the file
			lines.push_back({ tileSetPath + (fs::FileExists(tileSetPath) ? String(" - "_s + _x("editor", "written over, kept as .bak")) : String(" - "_s + _x("editor", "a copy in the user content"))), false });
			// TRANSLATORS: Header in the list of the files changing a tile set would write, {} is the number of levels
			lines.push_back({ _fx("editor", "Levels changed with it ({})", std::int32_t(state.WritableLevels.size())), true });
			for (const String& level : state.WritableLevels) {
				lines.push_back({ level, false });
			}
			if (!state.ReadOnlyLevels.empty()) {
				lines.push_back({ _fx("editor", "Levels that can't be changed, so the tile set can only be saved as a new one ({})", std::int32_t(state.ReadOnlyLevels.size())), true });
				for (const String& level : state.ReadOnlyLevels) {
					lines.push_back({ level, false });
				}
			}
			// The rules that refer to tiles of the tile set, in the user content
			SmallVector<std::int32_t, 0> newIndex;
			MakeNewIndex(state.Order, state.MergedInto, newIndex);
			SmallVector<String, 0> names;
			CollectRulesNames(names);
			SmallVector<FileLine, 0> rulesLines;
			for (const String& name : names) {
				Json::Value doc;
				if (RulesFile::Read(name, doc) && RemapRules(doc, IsSameTileSet(name, state.TileSet), state.TileSet, newIndex)) {
					String path = RulesFile::GetWritablePath(name, false);
					if (!path.empty()) {
						rulesLines.push_back({ Death::move(path), false });
					}
				}
			}
			if (!rulesLines.empty()) {
				// TRANSLATORS: Header in the list of the files changing a tile set would write, {} is the number of files of prefabs and terrain rules
				lines.push_back({ _fx("editor", "Prefabs and rules changed, kept as .bak ({})", std::int32_t(rulesLines.size())), true });
				for (FileLine& line : rulesLines) {
					lines.push_back(Death::move(line));
				}
			}
			lines.push_back({ _x("editor", "This level follows it in the editor, it's saved as usual"), true });
			// TRANSLATORS: Title of the list of the files changing a tile set would write
			ShowFileList(session, _x("editor", "Files That Would Change"), Death::move(lines), {}, {});
		}

		// Whether the level uses the tile set, as its default one or as an extra one that isn't stored in it
		bool DocumentUsesTileSet(const LevelDocument& document, StringView tileSet)
		{
			if (IsSameTileSet(document.Properties.DefaultTileSet, tileSet)) {
				return true;
			}
			for (const Formats::ExtraTileSetRecord& record : document.ExtraTileSets) {
				if ((record.Flags & Formats::ExtraTileSetRecord::Embedded) == 0 && IsSameTileSet(record.Name, tileSet)) {
					return true;
				}
			}
			return false;
		}

		// Puts back the files kept as .bak when the tile set was changed, the tile set, the levels that use it and the
		// rules that refer to it
		void ShowRestoreBackups(IEditorContext& session, std::shared_ptr<LayoutState> state, UiDialog* layoutDialog)
		{
			SmallVector<String, 0> files;
			String tileSetPath = ContentTileSetLibrary::GetWritablePath(state->TileSet, false);
			bool tileSetRestored = (!tileSetPath.empty() && fs::FileExists(String(tileSetPath + ".bak"_s)));
			if (tileSetRestored) {
				files.push_back(tileSetPath);
			}
			for (const String& level : state->WritableLevels) {
				if (fs::FileExists(String(level + ".bak"_s))) {
					files.push_back(level);
				}
			}
			// Only the rules that have tiles of the tile set, a backup of others would be of other changes
			SmallVector<String, 0> rulesNames;
			CollectRulesNames(rulesNames);
			for (const String& name : rulesNames) {
				String path = RulesFile::GetWritablePath(name, false);
				Json::Value doc;
				if (!path.empty() && fs::FileExists(String(path + ".bak"_s)) && RulesFile::Read(name, doc) && RulesReferTo(doc, name, state->TileSet)) {
					files.push_back(Death::move(path));
				}
			}
			if (files.empty()) {
				session.ShowMessage(_x("editor", "There are no backups of the tile set and its levels"));
				return;
			}

			// The level in the editor follows the tile set as it was changed, it's opened again from its file, which is
			// as it was before then; one that isn't saved anywhere can only keep what it has
			String ownPath = session.GetFilePath();
			bool ownRestored = false;
			for (const String& file : files) {
				ownRestored |= (!ownPath.empty() && NormalizePath(file) == NormalizePath(ownPath));
			}
			bool ownFollows = (tileSetRestored && DocumentUsesTileSet(session.GetDocument(), state->TileSet));
			bool reopen = ((ownRestored || ownFollows) && !ownPath.empty() && fs::FileExists(ownPath));

			SmallVector<FileLine, 0> lines;
			lines.push_back({ _x("editor", "These files get their previous content back, from their .bak files:"), true });
			for (const String& file : files) {
				lines.push_back({ file, false });
			}
			lines.push_back({ _x("editor", "Backups are made whenever a file is written over, they may be of different changes"), true });
			if (reopen) {
				lines.push_back({ session.GetHistory().IsDirty()
					? _x("editor", "This level is opened again from its file, the changes that weren't saved are lost")
					: _x("editor", "This level is opened again from its file"), true });
			} else if (ownFollows) {
				lines.push_back({ _x("editor", "This level isn't saved, it keeps the changed order and would show other tiles"), true });
			}
			IEditorContext* s = &session;
			// TRANSLATORS: Title and button of the dialog that puts back the tile set and its levels from their .bak files
			ShowFileList(session, _x("editor", "Restore Backups"), Death::move(lines), _x("editor", "Restore"), [s, state, files = Death::move(files), layoutDialog, reopen]() {
				std::int32_t restored = 0;
				for (const String& file : files) {
					String backupPath = file + ".bak"_s;
					auto stream = fs::Open(backupPath, FileAccess::Read);
					std::int64_t size = (stream->IsValid() ? stream->GetSize() : -1);
					if (size < 0) {
						LOGW("Backup \"{}\" can't be read", backupPath);
						continue;
					}
					SmallVector<std::uint8_t, 0> bytes;
					bytes.resize_for_overwrite(std::size_t(size));
					if (size > 0 && stream->Read(bytes.data(), size) != size) {
						LOGW("Backup \"{}\" can't be read", backupPath);
						continue;
					}
					stream = nullptr;
					// The backup stays, it can be put back again
					if (!AtomicFile::Write(file, bytes.data(), std::int64_t(bytes.size()))) {
						continue;
					}
					restored++;
				}
				s->InvalidateTileSet(state->TileSet);
				if (layoutDialog != nullptr) {
					layoutDialog->Close();
				}
				if (reopen && restored > 0) {
					// Asked for already, what wasn't saved is gone with the change it followed
					s->GetHistory().MarkSaved();
					s->OpenFile(s->GetFilePath());
				}
				s->ShowMessage(_fnx("editor", "{} file got its previous content back", "{} files got their previous content back", restored, restored));
			});
		}

		/**
		 * Saves the tile set itself in its new order, with the levels that use it and the rules that refer to it. The
		 * levels are changed off the user interface, everything is written next to its file first, the levels while their
		 * tile set is still the old one, which they are read with. Only when all of it could be written, the files are
		 * replaced, so a level that can't be changed doesn't leave the others in the new order and itself in the old one.
		 */
		void ChangeTileSet(IEditorContext& session, std::shared_ptr<LayoutState> state, UiDialog* layoutDialog)
		{
			const Formats::TileSetData* original = session.GetTileSetData(state->TileSet);
			String tileSetPath = ContentTileSetLibrary::GetWritablePath(state->TileSet, true);
			if (original == nullptr || tileSetPath.empty()) {
				session.ShowMessage(_x("editor", "There's no place to save tile sets to"));
				return;
			}

			// What the work needs is taken here, it doesn't touch the session
			struct ChangeJob
			{
				String TileSet;
				SmallVector<std::int32_t, 0> Order;
				SmallVector<std::int32_t, 0> MergedInto;
				SmallVector<String, 0> Levels;
				FixedTileCounts TileCounts;
				Formats::TileSetData NewTileSet;
				String TileSetPath;
				bool TileSetExisted = false;
				SmallVector<StagedFile, 0> Staged;
				// The level that couldn't be changed, or whether the tile set couldn't be written
				String FailedLevel;
				bool TileSetFailed = false;
			};
			auto job = std::make_shared<ChangeJob>();
			job->TileSet = state->TileSet;
			job->Order = state->Order;
			job->MergedInto = state->MergedInto;
			job->Levels = state->WritableLevels;
			job->TileCounts.Set(state->TileSet, state->OriginalCount);
			for (const LevelUse& use : state->Uses) {
				if (use.Writable && !use.DefaultTileSet.empty() && !job->TileCounts.Contains(use.DefaultTileSet)) {
					job->TileCounts.Set(use.DefaultTileSet, session.GetTileSetLibrary().GetTileCount(use.DefaultTileSet));
				}
			}
			BuildTileSet(*original, state->Order, job->NewTileSet);
			job->TileSetPath = Death::move(tileSetPath);
			job->TileSetExisted = fs::FileExists(job->TileSetPath);

			IEditorContext* s = &session;
			// TRANSLATORS: Title of the progress dialog while a rearranged tile set and its levels are saved
			RunWithProgress(session, _x("editor", "Changing the Tile Set"), [job](JobProgress& progress) {
				std::size_t total = job->Levels.size() + 1;
				for (std::size_t i = 0; i < job->Levels.size(); i++) {
					if (!progress.Report(float(i) / float(total))) {
						return;
					}
					const String& level = job->Levels[i];
					if (!StageLevelFile(job->TileCounts, level, job->TileSet, job->Order, job->MergedInto)) {
						job->FailedLevel = level;
						return;
					}
					job->Staged.push_back({ level, true });
				}
				if (!AtomicFile::Stage(job->TileSetPath, [&job](Stream& stream) { return Formats::TileSetFile::Write(stream, job->NewTileSet); })) {
					job->TileSetFailed = true;
					return;
				}
				job->Staged.push_back({ job->TileSetPath, job->TileSetExisted });
				progress.Report(1.0f);
			}, [s, job, layoutDialog](bool cancelled) {
				if (cancelled || !job->FailedLevel.empty() || job->TileSetFailed) {
					DiscardStaged(job->Staged);
					if (!job->FailedLevel.empty()) {
						LOGE("Level \"{}\" couldn't be changed to the new order of its tile set, nothing was changed", job->FailedLevel);
						s->ShowMessage(_fx("editor", "Level \"{}\" couldn't be changed, so nothing was changed", fs::GetFileName(job->FailedLevel)));
					} else if (job->TileSetFailed) {
						s->ShowMessage(_x("editor", "The tile set couldn't be saved, so nothing was changed"));
					} else {
						s->ShowMessage(_x("editor", "Changing the tile set was stopped, nothing was changed"));
					}
					return;
				}

				// The rules are small, they're staged here and replaced with the rest
				SmallVector<std::int32_t, 0> newIndex;
				MakeNewIndex(job->Order, job->MergedInto, newIndex);
				String failedRules;
				if (!StageAllRules(job->TileSet, newIndex, job->Staged, failedRules)) {
					DiscardStaged(job->Staged);
					s->ShowMessage(_fx("editor", "Prefabs and rules of tile set \"{}\" can't be read or changed, so nothing was changed", failedRules));
					return;
				}
				std::int32_t failed = CommitStaged(job->Staged);
				if (failed >= 0) {
					s->ShowMessage(_fx("editor", "File \"{}\" couldn't be replaced, so nothing was changed", fs::GetFileName(job->Staged[failed].Path)));
					return;
				}

				s->InvalidateTileSet(job->TileSet);
				// TRANSLATORS: Name of an undo step in the history
				ApplyToDocument(*s, job->Order, job->MergedInto, job->TileSet, {}, _x("editor", "Rearrange tile set"));
				// The files can't be changed back, an undo would leave the level in the old order with the new tile set
				s->GetHistory().Clear();
				s->GetHistory().MarkDirty();
				if (layoutDialog != nullptr) {
					layoutDialog->Close();
				}
				std::int32_t changed = std::int32_t(job->Levels.size());
				s->ShowMessage(_fnx("editor", "The tile set was changed with {} level", "The tile set was changed with {} levels", changed, changed));
			});
		}
	}

	static void ShowLayoutWindow(IEditorContext& session, std::shared_ptr<LayoutState> state)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		IEditorContext* s = &session;
		// TRANSLATORS: Title of the dialog that changes the order of the tiles of a tile set, {} is the name of the tile set
		auto dialog = std::make_unique<LayoutWindow>(_fx("editor", "Rearrange Tile Set \"{}\"", state->TileSet));
		UiDialog* dialogPtr = dialog.get();
		// The controls on the left, the tiles on the right
		auto* columns = dialog->GetContent()->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing * 2.0f);
		auto* content = columns->Add<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
		content->Width = 300.0f;
		columns->Add<UiSeparator>(UiOrientation::Vertical);
		{
			auto* gridColumn = columns->Add<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
			gridColumn->Add<UiWrappedLabel>(_x("editor", "Click a tile to select it, Ctrl+click another one, Shift+click a range - drag them to move them"), UiTheme::TextDim);
			auto* scroll = gridColumn->Add<UiScrollView>();
			scroll->Height = 200.0f;
			dialog->Grid = scroll->SetContent<LayoutGrid>(session, state);
			dialog->GridScroll = scroll;
			dialog->GridColumn = gridColumn;
		}

		// TRANSLATORS: Header of the buttons that change the selected tiles in the Rearrange Tile Set dialog
		content->Add<UiLabel>(_x("editor", "Selected tiles"), UiTheme::TextHeader);
		auto* row = content->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
		auto* secondRow = content->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
		auto addOperation = [state](UiStack* buttons, StringView text, EditorIcon icon, StringView tooltip, Function<void()>&& operation) {
			auto* button = buttons->Add<UiButton>(text, icon, Death::move(operation));
			button->Style = UiButtonStyle::Flat;
			button->Tooltip = tooltip;
			button->IsEnabledFunc = [state]() { return state->HasSelection(); };
			return button;
		};
		// TRANSLATORS: Button in the Rearrange Tile Set dialog, moves the selected tiles a place earlier, keep it short
		addOperation(row, _x("editor", "Earlier"), EditorIcon::ChevronLeft, _x("editor", "Move the selected tiles a place earlier"), [state]() {
			MoveSelectedBy(*state, -1);
		});
		// TRANSLATORS: Button in the Rearrange Tile Set dialog, moves the selected tiles a place later, keep it short
		addOperation(row, _x("editor", "Later"), EditorIcon::ChevronRight, _x("editor", "Move the selected tiles a place later"), [state]() {
			MoveSelectedBy(*state, 1);
		});
		// TRANSLATORS: Button in the Rearrange Tile Set dialog, removes the selected tiles, a verb
		addOperation(secondRow, _x("editor", "Delete"), EditorIcon::Delete, _x("editor", "Remove the selected tiles, the tiles after them move up"), [state]() {
			DeleteSelected(*state);
		});
		// TRANSLATORS: Button in the Rearrange Tile Set dialog, puts an empty tile before the selected ones, keep it short
		addOperation(secondRow, _x("editor", "Insert Empty"), EditorIcon::Plus, _x("editor", "Put an empty tile before the selected ones"), [state]() {
			InsertEmptyBeforeSelected(*state);
		});
		auto* reset = row->Add<UiButton>(_("Reset"), EditorIcon::Undo, [state]() {
			state->Reset();
		});
		reset->Style = UiButtonStyle::Flat;
		reset->Tooltip = _x("editor", "Put all tiles back where they were");

		// Tiles that can go, found in all levels that use the tile set
		content->Add<UiSeparator>();
		// TRANSLATORS: Header of the buttons that leave out duplicate and unused tiles in the Rearrange Tile Set dialog, a noun
		content->Add<UiLabel>(_x("editor", "Clean up"), UiTheme::TextHeader);
		auto* cleanRow = content->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
		// TRANSLATORS: Button in the Rearrange Tile Set dialog, leaves out the tiles that are the same as another one
		auto* merge = cleanRow->Add<UiButton>(_x("editor", "Merge Duplicates"), EditorIcon::Copy, [s, state]() {
			const Formats::TileSetData* data = s->GetTileSetData(state->TileSet);
			if (data == nullptr) {
				s->ShowMessage(_x("editor", "The tile set can't be read"));
				return;
			}
			std::int32_t merged = MergeDuplicates(*state, *data);
			s->ShowMessage(merged > 0
				? String(_fnx("editor", "{} tile was the same as another one, the levels show that one instead", "{} tiles were the same as others, the levels show those instead", merged, merged))
				: String(_x("editor", "No tile is the same as another one")));
		});
		merge->Style = UiButtonStyle::Flat;
		merge->Tooltip = _x("editor", "Leave out the tiles that look the same as one before them and have the same mask, the levels show that one instead");
		// TRANSLATORS: Button in the Rearrange Tile Set dialog, leaves out the tiles no level uses
		auto* unused = cleanRow->Add<UiButton>(_x("editor", "Remove Unused"), EditorIcon::Delete, [s, state]() {
			// The levels are read off the user interface, they know their default tile sets, the counts of tiles come
			// from the editor
			struct UsageJob
			{
				SmallVector<LevelUse, 0> Uses;
				SmallVector<std::int32_t, 0> DefaultCounts;
				String TileSet;
				SmallVector<bool, 0> Used;
			};
			auto job = std::make_shared<UsageJob>();
			job->TileSet = state->TileSet;
			job->Uses = state->Uses;
			for (const LevelUse& use : job->Uses) {
				job->DefaultCounts.push_back(use.Default ? state->OriginalCount : s->GetTileSetLibrary().GetTileCount(use.DefaultTileSet));
			}
			job->Used.resize(std::size_t(state->OriginalCount), false);
			MarkOwnUsage(*s, state->TileSet, job->Used);
			// TRANSLATORS: Title of the progress dialog while the levels are searched for the tiles they use
			RunWithProgress(*s, _x("editor", "Finding Unused Tiles"), [job](JobProgress& progress) {
				for (std::size_t i = 0; i < job->Uses.size(); i++) {
					if (!progress.Report(float(i) / float(job->Uses.size()))) {
						return;
					}
					MarkLevelUsage(job->Uses[i], job->TileSet, job->DefaultCounts[i], job->Used);
				}
			}, [s, state, job](bool cancelled) {
				if (cancelled) {
					return;
				}
				std::int32_t removed = RemoveUnused(*state, job->Used);
				std::int32_t levels = std::int32_t(job->Uses.size()) + 1;
				s->ShowMessage(removed > 0
					? String(_fnx("editor", "{} tile no level uses was left out", "{} tiles no level uses were left out", removed, removed) + " ("_s +
						// TRANSLATORS: In parentheses after the number of unused tiles left out, the number of levels that were searched
						_fnx("editor", "{} level looked at", "{} levels looked at", levels, levels) + ")"_s)
					: String(_x("editor", "Every tile is used by a level, a prefab or a rule")));
			});
		});
		unused->Style = UiButtonStyle::Flat;
		unused->Tooltip = _x("editor", "Leave out the tiles that neither this level nor any level found that uses the tile set shows, nor its prefabs and rules");

		auto* status = content->Add<UiWrappedLabel>(StringView(), UiTheme::TextDim);
		status->TextFunc = [state]() {
			std::int32_t moved = 0, inserted = 0;
			SmallVector<bool, 0> kept;
			kept.resize(std::size_t(state->OriginalCount), false);
			for (std::int32_t i = 0; i < std::int32_t(state->Order.size()); i++) {
				std::int32_t from = state->Order[i];
				if (from < 0) {
					inserted++;
				} else {
					kept[from] = true;
					moved += (from != i ? 1 : 0);
				}
			}
			std::int32_t removed = 0, merged = 0;
			for (std::size_t i = 0; i < kept.size(); i++) {
				if (!kept[i]) {
					bool isMerged = (i < state->MergedInto.size() && state->MergedInto[i] >= 0);
					(isMerged ? merged : removed)++;
				}
			}
			// TRANSLATORS: Summary in the Rearrange Tile Set dialog, the number of tiles, then of the moved, removed, merged and inserted empty tiles
			return String(_fx("editor", "{} tiles - {} in another place, {} removed, {} merged, {} new empty ones", std::int32_t(state->Order.size()), moved, removed, merged, inserted));
		};

		std::int32_t writable = std::int32_t(state->WritableLevels.size());
		std::int32_t readOnly = std::int32_t(state->ReadOnlyLevels.size());
		content->Add<UiSeparator>();
		// TRANSLATORS: Header of the levels that use the tile set in the Rearrange Tile Set dialog
		content->Add<UiLabel>(_x("editor", "Levels"), UiTheme::TextHeader);
		content->Add<UiWrappedLabel>(_fx("editor", "Used by {} levels - {} of them are changed with the tile set", writable + readOnly, writable), UiTheme::TextDim);
		if (readOnly > 0) {
			content->Add<UiWrappedLabel>(_fnx("editor", "{} level is in the files of the game and can't be changed, it would show other tiles",
				"{} levels are in the files of the game and can't be changed, they would show other tiles", readOnly, readOnly), UiTheme::TextDanger);
			content->Add<UiWrappedLabel>(_x("editor", "So the tile set itself can't be changed, saving it as a new tile set changes only this level and leaves the others as they are"), UiTheme::TextDim);
		}
		content->Add<UiWrappedLabel>(_x("editor", "Levels are looked for in the episodes and next to this level, levels elsewhere aren't changed"), UiTheme::TextDim);

		content->Add<UiSeparator>();
		auto* nameRow = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
		// TRANSLATORS: Label of the name of the tile set saved as a new one in the Rearrange Tile Set dialog
		nameRow->Add<UiLabel>(_x("editor", "New name"), UiTheme::TextDim);
		auto* nameBox = nameRow->Add<UiTextBox>(SuggestTileSetName(state->TileSet, "-new"_s));
		nameBox->Flex = 1.0f;
		nameBox->MaxLength = 32;

		// TRANSLATORS: Button in the Rearrange Tile Set dialog
		dialog->AddButton(_x("editor", "Save as New Tile Set"), [s, state, nameBox, dialogPtr]() {
			if (!state->IsChanged()) {
				dialogPtr->Close();
				return;
			}
			String name = nameBox->Text.trimmed();
			if (!ContentTileSetLibrary::IsValidName(name)) {
				s->ShowMessage(_x("editor", "A tile set is named by lowercase letters, digits, dashes and underscores"));
				return;
			}
			if (!ContentTileSetLibrary::FindTileSetFile(name).empty()) {
				s->ShowMessage(_fx("editor", "A tile set \"{}\" exists already", name));
				return;
			}
			const Formats::TileSetData* original = s->GetTileSetData(state->TileSet);
			String path = ContentTileSetLibrary::GetWritablePath(name, true);
			if (original == nullptr || path.empty()) {
				s->ShowMessage(_x("editor", "There's no place to save tile sets to"));
				return;
			}
			Formats::TileSetData tileSet;
			BuildTileSet(*original, state->Order, tileSet);
			if (!AtomicFile::Write(path, [&tileSet](Stream& stream) { return Formats::TileSetFile::Write(stream, tileSet); })) {
				s->ShowMessage(_x("editor", "The tile set couldn't be saved"));
				return;
			}
			// The prefabs and terrain rules of the tile set go with the new one, in its order
			Json::Value rules;
			if (RulesFile::Read(state->TileSet, rules)) {
				SmallVector<std::int32_t, 0> newIndex;
				MakeNewIndex(state->Order, state->MergedInto, newIndex);
				RemapRules(rules, true, state->TileSet, newIndex);
				if (!RulesFile::Write(name, rules)) {
					LOGW("Rules of tile set \"{}\" couldn't be copied to \"{}\"", state->TileSet, name);
				}
			}
			s->InvalidateTileSet(name);
			// The old tile set is still there as it was, so this can be undone
			ApplyToDocument(*s, state->Order, state->MergedInto, state->TileSet, name, _x("editor", "Rearrange tile set"));
			dialogPtr->Close();
			s->ShowMessage(_fx("editor", "The level uses the new tile set \"{}\" now", name));
		}, true);
		// TRANSLATORS: Button in the Rearrange Tile Set dialog, saves the tile set itself and the levels that use it
		auto* changeButton = dialog->AddButton(_x("editor", "Change the Tile Set"), [s, state, dialogPtr]() {
			if (!state->IsChanged()) {
				dialogPtr->Close();
				return;
			}
			// The changed copy in the user content would be used by the levels of the game too, they'd show other tiles
			if (!state->ReadOnlyLevels.empty()) {
				s->ShowMessage(_x("editor", "Levels of the game use the tile set, it can only be saved as a new one"));
				return;
			}
			// Another editor would put a level back in the old order by saving it
			SmallVector<FileLine, 0> locked;
			for (const String& level : state->WritableLevels) {
				if (IsLevelOpenElsewhere(level, s->GetFilePath())) {
					locked.push_back({ level, false });
				}
			}
			if (!locked.empty()) {
				locked.insert(locked.begin(), FileLine{ _x("editor", "These levels are open in another editor, close them there first:"), true });
				ShowFileList(*s, _x("editor", "Change the Tile Set"), Death::move(locked), {}, {});
				return;
			}
			std::int32_t writable = std::int32_t(state->WritableLevels.size());
			String text = _fx("editor", "The tile set and {} levels that use it are saved changed, the previous files are kept as .bak files.", writable);
			text += "\n"_s + _x("editor", "This can't be undone, the history of changes of this level is cleared.");
			// TRANSLATORS: Title of the question before the tile set and its levels are saved, and its button that confirms it
			UiMessageDialog::Show(*s->GetSurface(), _x("editor", "Change the Tile Set"), text, { _x("editor", "Change It"), _x("editor", "Cancel") }, [s, state, dialogPtr](std::int32_t choice) {
				if (choice == 0) {
					ChangeTileSet(*s, state, dialogPtr);
				}
			});
		});
		changeButton->IsEnabledFunc = [state]() { return state->ReadOnlyLevels.empty(); };
		if (readOnly > 0) {
			changeButton->Tooltip = _x("editor", "Levels of the game use the tile set, it can only be saved as a new one");
		}
		dialog->AddButton(_x("editor", "Cancel"), [dialogPtr]() { dialogPtr->Close(); });

		// What would be written, and putting back what was
		{
			auto* filesRow = content->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
			// TRANSLATORS: Button in the Rearrange Tile Set dialog, lists the files that would be written
			auto* files = filesRow->Add<UiButton>(_x("editor", "Files"), EditorIcon::Folder, [s, state]() {
				ShowChangedFiles(*s, *state);
			});
			files->Style = UiButtonStyle::Flat;
			files->Tooltip = _x("editor", "List the files changing the tile set would write, nothing is written");
			auto* restore = filesRow->Add<UiButton>(_x("editor", "Restore Backups"), EditorIcon::Undo, [s, state, dialogPtr]() {
				ShowRestoreBackups(*s, state, dialogPtr);
			});
			restore->Style = UiButtonStyle::Flat;
			restore->Tooltip = _x("editor", "Put back the tile set and its levels as they were before they were last written over, from their .bak files");
		}

		ShowModal(*surface, Death::move(dialog));
	}

	void ShowTileSetLayoutDialog(IEditorContext& session)
	{
		if (session.GetSurface() == nullptr) {
			return;
		}
		LevelDocument& document = session.GetDocument();
		// The name comes from the level, it must not lead out of where tile sets are saved to
		if (!Formats::LevelFile::IsSafeTileSetPath(document.Properties.DefaultTileSet)) {
			session.ShowMessage(_x("editor", "The tile set of the level has a name that can't be saved"));
			return;
		}
		const Formats::TileSetData* data = session.GetTileSetData(document.Properties.DefaultTileSet);
		if (data == nullptr) {
			session.ShowMessage(_x("editor", "The tile set of the level can't be read"));
			return;
		}

		auto state = std::make_shared<LayoutState>();
		state->TileSet = document.Properties.DefaultTileSet;
		state->OriginalCount = data->TileCount;
		state->Reset();

		// The levels that use the tile set are looked for off the user interface, there can be hundreds of them
		auto input = std::make_shared<ScanInput>(PrepareScan(session, state->TileSet));
		auto uses = std::make_shared<SmallVector<LevelUse, 0>>();
		IEditorContext* s = &session;
		// TRANSLATORS: Title of the progress dialog while the levels that use the tile set are searched for
		RunWithProgress(session, _x("editor", "Looking for Levels of the Tile Set"), [input, uses](JobProgress& progress) {
			ScanLevels(*input, *uses, &progress);
		}, [s, state, uses](bool cancelled) {
			if (cancelled) {
				return;
			}
			state->Uses = Death::move(*uses);
			for (const LevelUse& use : state->Uses) {
				(use.Writable ? state->WritableLevels : state->ReadOnlyLevels).push_back(use.Path);
			}
			ShowLayoutWindow(*s, state);
		});
	}

	bool WriteTileSetWithAddedTiles(IEditorContext& session, StringView tileSet, StringView path, const Formats::TileSetData& data, std::int32_t oldCount)
	{
		if (!Formats::LevelFile::IsSafeTileSetPath(tileSet) || oldCount < 0 || oldCount > data.TileCount) {
			session.ShowMessage(_x("editor", "The tile set couldn't be saved"));
			return false;
		}

		// The levels are only looked into, from their start, it's done before any tiles are added
		ScanInput input = PrepareScan(session, tileSet);
		SmallVector<LevelUse, 0> uses;
		ScanLevels(input, uses, nullptr);
		SmallVector<String, 0> changed;
		SmallVector<FileLine, 0> readOnly, locked;
		for (LevelUse& use : uses) {
			// The tiles of its extra tile sets follow those of the default one, they'd move by the tiles added
			if (!use.Default || use.Extras.empty()) {
				continue;
			}
			if (!use.Writable) {
				readOnly.push_back({ Death::move(use.Path), false });
			} else if (IsLevelOpenElsewhere(use.Path, session.GetFilePath())) {
				locked.push_back({ Death::move(use.Path), false });
			} else {
				changed.push_back(Death::move(use.Path));
			}
		}
		if (!readOnly.empty()) {
			readOnly.insert(readOnly.begin(), FileLine{ _fx("editor", "These levels of the game have more tile sets after \"{}\", they would show other tiles. Add the tiles to a new tile set or to the tiles of this level instead:", tileSet), true });
			ShowFileList(session, _x("editor", "Tiles Can't Be Added"), Death::move(readOnly), {}, {});
			return false;
		}
		if (!locked.empty()) {
			locked.insert(locked.begin(), FileLine{ _x("editor", "These levels use the tile set and are open in another editor, close them there first:"), true });
			ShowFileList(session, _x("editor", "Tiles Can't Be Added"), Death::move(locked), {}, {});
			return false;
		}

		// The levels get the tiles at the end of their default tile set too, so the IDs after them move, everything is
		// written next to its file first and replaced only once all of it could be written
		FixedTileCounts tileCounts;
		tileCounts.Set(tileSet, oldCount);
		SmallVector<std::int32_t, 0> order;
		order.reserve(std::size_t(data.TileCount));
		for (std::int32_t i = 0; i < data.TileCount; i++) {
			order.push_back(i < oldCount ? i : -1);
		}
		SmallVector<StagedFile, 0> staged;
		for (const String& level : changed) {
			if (!StageLevelFile(tileCounts, level, tileSet, order, {})) {
				LOGE("Level \"{}\" couldn't be changed for the tiles added to tile set \"{}\", nothing was changed", level, tileSet);
				DiscardStaged(staged);
				session.ShowMessage(_fx("editor", "Level \"{}\" couldn't be changed, so nothing was changed", fs::GetFileName(level)));
				return false;
			}
			staged.push_back({ level, true });
		}
		bool existed = fs::FileExists(path);
		if (!AtomicFile::Stage(path, [&data](Stream& s) { return Formats::TileSetFile::Write(s, data); })) {
			DiscardStaged(staged);
			session.ShowMessage(_x("editor", "The tile set couldn't be saved"));
			return false;
		}
		staged.push_back({ String(path), existed });
		std::int32_t failed = CommitStaged(staged);
		if (failed >= 0) {
			session.ShowMessage(_fx("editor", "File \"{}\" couldn't be replaced, so nothing was changed", fs::GetFileName(staged[failed].Path)));
			return false;
		}
		if (!changed.empty()) {
			LOGI("{} levels that use tile set \"{}\" were changed with the tiles added to it", std::int32_t(changed.size()), tileSet);
		}
		return true;
	}
}

#endif
