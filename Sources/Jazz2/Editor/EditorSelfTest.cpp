#include "EditorSelfTest.h"

#if defined(WITH_EDITOR) && defined(DEATH_DEBUG)

#include "Document/Commands.h"
#include "Document/DocumentViewState.h"
#include "Events/EventCatalog.h"
#include "Files/ContentTileSetLibrary.h"
#include "Imaging/PaletteFile.h"
#include "Imaging/PaletteMatch.h"
#include "Imaging/PngCodec.h"
#include "Imaging/TileImages.h"
#include "Shell/FxTileGenerator.h"
#include "Shell/LevelCheck.h"
#include "Shell/TileSetLayoutDialog.h"
#include "../Formats/TileSetFile.h"
#include "../ContentResolver.h"
#include "../Rendering/PolylineRenderer.h"

#include <Containers/String.h>
#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>
#include <IO/MemoryStream.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		enum class TestResult {
			Identical,
			Equivalent,
			NotNative,
			Failed
		};

		// Small and deterministic, so a failure can be reproduced
		class TestRandom
		{
		public:
			explicit TestRandom(std::uint32_t seed)
				: _state(seed != 0 ? seed : 0x9E3779B9u) {}

			std::uint32_t Next() {
				_state ^= _state << 13;
				_state ^= _state >> 17;
				_state ^= _state << 5;
				return _state;
			}

			std::int32_t Next(std::int32_t max) {
				return (max > 0 ? std::int32_t(Next() % std::uint32_t(max)) : 0);
			}

		private:
			std::uint32_t _state;
		};

		void CollectFiles(StringView path, SmallVectorImpl<String>& files)
		{
			for (auto item : fs::Directory(path, fs::EnumerationOptions::SkipDirectories)) {
				if (fs::GetExtension(item) == "j2l"_s) {
					files.push_back(item);
				}
			}
			for (auto item : fs::Directory(path, fs::EnumerationOptions::SkipFiles)) {
				CollectFiles(item, files);
			}
		}

		bool Export(const LevelDocument& document, MemoryStream& output, bool exportOnly = true)
		{
			output.Seek(0, SeekOrigin::Begin);
			output.SetSize(0);
			return document.Save(output, exportOnly);
		}

		bool AreEqual(const MemoryStream& a, const MemoryStream& b)
		{
			return (a.GetSize() == b.GetSize() && std::memcmp(a.GetBuffer(), b.GetBuffer(), std::size_t(a.GetSize())) == 0);
		}

		bool AreEqual(const MemoryStream& a, ArrayView<const std::uint8_t> b)
		{
			return (std::size_t(a.GetSize()) == b.size() && std::memcmp(a.GetBuffer(), b.data(), b.size()) == 0);
		}

		bool TestEditorData(StringView path, const LevelDocument& original, const MemoryStream& expected, ITileSetLibrary& library)
		{
			constexpr std::uint32_t TestChunk = Formats::LevelFile::MakeChunkTag('e', 't', 's', 't');
			static const std::uint8_t TestChunkData[] = { 1, 2, 3, 4, 5 };

			// Layer names, layer states and an editor chunk are saved...
			LevelDocument document;
			{
				MemoryStream copy;
				original.Save(copy, false);
				copy.Seek(0, SeekOrigin::Begin);
				document.Load(copy, &library);
			}
			for (std::size_t i = 0; i < document.Layers.size(); i++) {
				char letter = char('A' + (i % 26));
				document.Layers[i].Name = "Layer "_s + StringView(&letter, 1);
				document.Layers[i].EditorFlags = std::uint8_t(i & 3);
			}
			document.SetChunk(TestChunk, TestChunkData);

			MemoryStream saved;
			if (!document.Save(saved, false)) {
				LOGE("[{}] Level with editor data cannot be saved", path);
				return false;
			}

			// ...and loaded back
			LevelDocument reloaded;
			saved.Seek(0, SeekOrigin::Begin);
			if (!reloaded.Load(saved, &library) || reloaded.Layers.size() != document.Layers.size()) {
				LOGE("[{}] Level with editor data cannot be loaded", path);
				return false;
			}
			for (std::size_t i = 0; i < reloaded.Layers.size(); i++) {
				if (reloaded.Layers[i].Name != document.Layers[i].Name || reloaded.Layers[i].EditorFlags != document.Layers[i].EditorFlags) {
					LOGE("[{}] Layer {} lost its name or state", path, i);
					return false;
				}
			}
			const Formats::ChunkRecord* chunk = reloaded.FindChunk(TestChunk);
			if (chunk == nullptr || chunk->Data.size() != sizeof(TestChunkData) || std::memcmp(chunk->Data.data(), TestChunkData, sizeof(TestChunkData)) != 0) {
				LOGE("[{}] Editor chunk was lost", path);
				return false;
			}

			// The export drops the editor data, the layer names too, only the editor uses them
			MemoryStream exported;
			Export(reloaded, exported);
			LevelDocument exportedDocument;
			exported.Seek(0, SeekOrigin::Begin);
			if (!exportedDocument.Load(exported, &library) || exportedDocument.FindChunk(TestChunk) != nullptr ||
				exportedDocument.Layers.empty() || exportedDocument.Layers[0].EditorFlags != 0 || !exportedDocument.Layers[0].Name.empty()) {
				LOGE("[{}] Export kept the editor data", path);
				return false;
			}

			for (DocumentLayer& layer : reloaded.Layers) {
				layer.Name = {};
				layer.EditorFlags = 0;
			}
			reloaded.SetChunk(TestChunk, {});
			Export(reloaded, exported);
			if (!AreEqual(exported, expected)) {
				LOGE("[{}] Level differs after editor data were added and removed", path);
				return false;
			}
			return true;
		}

		TileRef MakeRandomTile(TestRandom& random, const LevelDocument& document)
		{
			TileRef tile;
			tile.Flags = std::uint8_t(random.Next(4) | (random.Next(3) << 4));
			std::int32_t kind = random.Next(10);
			if (kind == 0 && !document.Animations.empty()) {
				tile.Source = TileRef::Animated;
				tile.Index = std::uint16_t(random.Next(std::int32_t(document.Animations.size())));
			} else {
				tile.Source = TileRef::DefaultTileSet;
				tile.Index = std::uint16_t(random.Next(document.DefaultTileCount > 0 ? document.DefaultTileCount : 256));
			}
			return tile;
		}

		void FillTiles(TestRandom& random, const LevelDocument& document, SetTilesCommand& command, std::int32_t layerIndex)
		{
			const DocumentLayer& layer = document.Layers[layerIndex];
			std::int32_t count = 1 + random.Next(random.Next(8) == 0 ? 4000 : 40);
			for (std::int32_t i = 0; i < count; i++) {
				command.Set(random.Next(layer.GetWidth()), random.Next(layer.GetHeight()), MakeRandomTile(random, document));
			}
		}

		void ApplyRandomCommand(TestRandom& random, CommandHistory& history, DocumentChanges& changes)
		{
			LevelDocument& document = history.GetDocument();
			std::int32_t layerIndex = random.Next(std::int32_t(document.Layers.size()));
			std::int32_t kind = random.Next(100);

			if (kind < 40) {
				if (document.Layers[layerIndex].Tiles.empty()) {
					return;
				}
				auto command = std::make_unique<SetTilesCommand>(document, layerIndex, "Tiles"_s);
				FillTiles(random, document, *command, layerIndex);
				history.Execute(std::move(command), changes);
			} else if (kind < 55) {
				Vector2i gridSize = document.GetEventGridSize();
				if (gridSize.X <= 0 || gridSize.Y <= 0) {
					return;
				}
				auto command = std::make_unique<SetEventsCommand>(document, "Events"_s);
				std::int32_t count = 1 + random.Next(20);
				for (std::int32_t i = 0; i < count; i++) {
					Formats::EventRecord event = LevelDocument::GetEmptyEvent();
					if (random.Next(4) != 0) {
						event.Type = std::uint16_t(1 + random.Next(300));
						event.Flags = std::uint8_t(random.Next(256));
						event.GeneratorFlags = std::uint8_t(random.Next(2));
						event.GeneratorDelay = std::uint8_t(random.Next(30));
						for (std::uint8_t& param : event.Params) {
							param = std::uint8_t(random.Next(3) == 0 ? random.Next(256) : 0);
						}
					}
					command->Set(random.Next(gridSize.X), random.Next(gridSize.Y), event);
				}
				history.Execute(std::move(command), changes);
			} else if (kind < 65) {
				Formats::LayerRecord record = document.Layers[layerIndex].Record;
				record.OffsetX += float(random.Next(64));
				record.SpeedY = float(random.Next(100)) / 50.0f;
				record.Flags ^= Formats::LayerRecord::RepeatX;
				auto command = std::make_unique<SetLayerPropertiesCommand>(document, layerIndex, record,
					random.Next(2) == 0 ? "Renamed"_s : StringView(document.Layers[layerIndex].Name), "Layer properties"_s);
				history.Execute(std::move(command), changes, random.Next(2) == 0);
			} else if (kind < 72) {
				Formats::LevelProperties properties = document.Properties;
				properties.DisplayName = properties.DisplayName + "!"_s;
				properties.WaterLevel = std::uint16_t(random.Next(UINT16_MAX));
				auto command = std::make_unique<SetLevelPropertiesCommand>(document, document.Flags ^ LevelFlags::HasLaps, properties, "Level properties"_s);
				history.Execute(std::move(command), changes, random.Next(2) == 0);
			} else if (kind < 82) {
				// Resize a layer by a column and a row, or add a new layer
				auto command = SnapshotCommand::Begin(document, SnapshotParts::Layers, "Layer structure"_s);
				if (random.Next(2) == 0 && !document.Layers[layerIndex].IsSpriteLayer()) {
					DocumentLayer& layer = document.Layers[layerIndex];
					std::int32_t oldWidth = layer.GetWidth(), oldHeight = layer.GetHeight();
					std::int32_t newWidth = oldWidth + 1, newHeight = oldHeight + 1;
					SmallVector<TileRef, 0> tiles;
					tiles.resize(std::size_t(newWidth) * std::size_t(newHeight), TileRef{});
					for (std::int32_t y = 0; y < oldHeight; y++) {
						for (std::int32_t x = 0; x < oldWidth; x++) {
							tiles[std::size_t(y) * newWidth + x] = layer.GetTile(x, y);
						}
					}
					layer.Tiles = std::move(tiles);
					layer.Record.Width = newWidth;
					layer.Record.Height = newHeight;
				} else if (document.Layers.size() < UINT8_MAX) {
					DocumentLayer layer;
					layer.Record = document.Layers[layerIndex].Record;
					layer.Record.Kind = Formats::LayerKind::Other;
					layer.Record.Width = 1 + random.Next(30);
					layer.Record.Height = 1 + random.Next(30);
					layer.Tiles.resize(std::size_t(layer.Record.Width) * std::size_t(layer.Record.Height), TileRef{});
					document.Layers.insert(document.Layers.begin() + random.Next(std::int32_t(document.Layers.size()) + 1), std::move(layer));
				}
				command->End(document, changes);
				history.Record(std::move(command));
			} else if (kind < 90) {
				history.BeginTransaction("Transaction"_s);
				for (std::int32_t i = 0; i < 3; i++) {
					std::int32_t index = random.Next(std::int32_t(document.Layers.size()));
					if (!document.Layers[index].Tiles.empty()) {
						auto command = std::make_unique<SetTilesCommand>(document, index, "Tiles"_s);
						FillTiles(random, document, *command, index);
						history.Execute(std::move(command), changes);
					}
				}
				if (random.Next(4) == 0) {
					history.CancelTransaction(changes);
				} else {
					history.EndTransaction();
				}
			} else if (kind < 95) {
				history.Undo(changes);
			} else {
				history.Redo(changes);
			}
		}

		bool TestCommands(StringView path, LevelDocument& document, const MemoryStream& expected, std::uint32_t seed)
		{
			if (document.Layers.empty()) {
				return true;
			}

			CommandHistory history(document, SIZE_MAX);
			DocumentChanges changes;
			TestRandom random(seed);

			std::int32_t count = 30 + random.Next(30);
			for (std::int32_t i = 0; i < count; i++) {
				ApplyRandomCommand(random, history, changes);
			}

			String problem;
			if (!document.Validate(&problem)) {
				LOGE("[{}] Document is not valid after random changes: {}", path, problem);
				return false;
			}

			MemoryStream changed;
			Export(document, changed);
			std::uint32_t changedRevision = history.GetRevision();
			// The sequence may have ended by undoing something, which then stays undone
			std::int32_t doneCount = history.GetDoneCount();

			std::int32_t undoCount = 0;
			while (history.Undo(changes)) {
				undoCount++;
			}
			if (history.IsDirty()) {
				LOGE("[{}] Document is dirty after everything was undone", path);
				return false;
			}

			MemoryStream undone;
			Export(document, undone);
			if (!AreEqual(undone, expected)) {
				LOGE("[{}] Level differs after {} changes were undone", path, undoCount);
				return false;
			}

			for (std::int32_t i = 0; i < doneCount; i++) {
				history.Redo(changes);
			}
			MemoryStream redone;
			Export(document, redone);
			if (!AreEqual(redone, changed) || history.GetRevision() != changedRevision) {
				LOGE("[{}] Level differs after {} changes were redone", path, undoCount);
				return false;
			}
			return true;
		}

		// Bits of the parameters a field takes, as a mask of the 128 bits
		void GetFieldBits(const EventParamField& field, std::uint8_t (&bits)[Formats::LevelFile::EventParamsSize])
		{
			std::memset(bits, 0, sizeof(bits));
			std::int32_t first = field.Offset * 8 + field.Shift;
			for (std::int32_t i = first; i < first + field.Bits && i < Formats::LevelFile::EventParamsSize * 8; i++) {
				bits[i / 8] |= std::uint8_t(1 << (i % 8));
			}
		}

		std::int32_t TestCatalogConsistency()
		{
			std::int32_t problems = 0;
			auto all = EventCatalog::GetAll();
			for (std::size_t i = 0; i < all.size(); i++) {
				const EventDescriptor& descriptor = *all[i];
				for (std::size_t j = 0; j < i; j++) {
					if (StringView(all[j]->Key) == StringView(descriptor.Key)) {
						LOGE("[Catalog] Key \"{}\" is used twice", descriptor.Key);
						problems++;
					}
				}
				if (descriptor.Name == nullptr || descriptor.Key == nullptr) {
					LOGE("[Catalog] Event {} has no name or key", std::uint32_t(descriptor.Type));
					problems++;
					continue;
				}

				std::uint8_t used[Formats::LevelFile::EventParamsSize] = {};
				Formats::EventRecord event = EventCatalog::MakeDefault(descriptor.Type);
				for (std::uint8_t f = 0; f < descriptor.FieldCount; f++) {
					const EventParamField& field = descriptor.Fields[f];
					bool isText = (field.Type == ParamType::String || field.Type == ParamType::Raw);
					std::int32_t lastBit = field.Shift + field.Bits;
					if (field.Bits == 0 || field.Offset * 8 + lastBit > Formats::LevelFile::EventParamsSize * 8 || (!isText && lastBit > 32)) {
						LOGE("[Catalog] {}.{} is outside of the parameters", descriptor.Key, field.Name);
						problems++;
						continue;
					}

					std::uint8_t bits[Formats::LevelFile::EventParamsSize];
					GetFieldBits(field, bits);
					for (std::int32_t b = 0; b < Formats::LevelFile::EventParamsSize; b++) {
						if ((used[b] & bits[b]) != 0) {
							LOGE("[Catalog] {}.{} overlaps another field at byte {}", descriptor.Key, field.Name, b);
							problems++;
							break;
						}
						used[b] |= bits[b];
					}
					if (isText) {
						continue;
					}

					std::int64_t lowest = (field.Signed ? -(std::int64_t(1) << (field.Bits - 1)) : 0);
					std::int64_t highest = (field.Signed ? (std::int64_t(1) << (field.Bits - 1)) - 1 : (std::int64_t(1) << field.Bits) - 1);
					if (field.Min < lowest || field.Max > highest || field.Min > field.Max) {
						LOGE("[Catalog] {}.{} has range {} to {}, but {} bits hold {} to {}", descriptor.Key, field.Name, field.Min, field.Max, field.Bits, lowest, highest);
						problems++;
					}
					if (field.Type != ParamType::Flags && (field.Default < field.Min || field.Default > field.Max)) {
						LOGE("[Catalog] {}.{} has default {} out of its range", descriptor.Key, field.Name, field.Default);
						problems++;
					}
					if (field.Type == ParamType::Enum || field.Type == ParamType::Flags) {
						bool defaultFound = (field.Type == ParamType::Flags);
						for (std::uint8_t o = 0; o < field.OptionCount; o++) {
							std::int64_t value = field.Options[o].Value;
							if (value < 0 || value > highest) {
								LOGE("[Catalog] {}.{} option \"{}\" doesn't fit", descriptor.Key, field.Name, field.Options[o].Label);
								problems++;
							}
							if (value == field.Default) {
								defaultFound = true;
							}
						}
						if (!defaultFound) {
							LOGW("[Catalog] {}.{} has default {}, which isn't one of its options", descriptor.Key, field.Name, field.Default);
						}
					}
					std::int32_t read = EventCatalog::ReadField(event, field);
					std::int32_t expected = (field.Type == ParamType::Flags ? field.Default & std::int32_t(highest) : field.Default);
					if (read != expected) {
						LOGE("[Catalog] {}.{} reads {} from a new event, the default is {}", descriptor.Key, field.Name, read, field.Default);
						problems++;
					}
				}
			}
			LOGI("Event catalog has {} events, {} problems", all.size(), problems);
			return problems;
		}

		// What the events of all tested levels hold that the catalog doesn't expect
		struct CatalogScan
		{
			struct FieldStats
			{
				std::int32_t OutOfRange = 0;
				std::int32_t NotAnOption = 0;
				std::int32_t Example = 0;
			};
			struct TypeStats
			{
				std::int32_t Count = 0;
				std::int32_t Uncovered = 0;
				std::uint8_t UncoveredBits[Formats::LevelFile::EventParamsSize] = {};
				SmallVector<FieldStats, 0> Fields;
			};

			SmallVector<TypeStats, 0> Types;

			CatalogScan()
			{
				Types.resize(65536);
			}

			void Add(const Formats::EventRecord& event)
			{
				TypeStats& stats = Types[event.Type];
				stats.Count++;
				const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type));
				if (descriptor == nullptr) {
					return;
				}

				if (stats.Fields.size() < descriptor->FieldCount) {
					stats.Fields.resize(descriptor->FieldCount);
				}
				std::uint8_t covered[Formats::LevelFile::EventParamsSize] = {};
				for (std::uint8_t f = 0; f < descriptor->FieldCount; f++) {
					const EventParamField& field = descriptor->Fields[f];
					std::uint8_t bits[Formats::LevelFile::EventParamsSize];
					GetFieldBits(field, bits);
					for (std::int32_t b = 0; b < Formats::LevelFile::EventParamsSize; b++) {
						covered[b] |= bits[b];
					}
					if (field.Type == ParamType::String || field.Type == ParamType::Raw) {
						continue;
					}

					std::int32_t value = EventCatalog::ReadField(event, field);
					FieldStats& fieldStats = stats.Fields[f];
					if (field.Type != ParamType::Flags && (value < field.Min || value > field.Max)) {
						fieldStats.OutOfRange++;
						fieldStats.Example = value;
					} else if (field.Type == ParamType::Enum && field.OptionCount > 0) {
						bool found = false;
						for (std::uint8_t o = 0; o < field.OptionCount && !found; o++) {
							found = (field.Options[o].Value == value);
						}
						if (!found) {
							fieldStats.NotAnOption++;
							fieldStats.Example = value;
						}
					} else if (field.Type == ParamType::Flags) {
						std::int32_t known = 0;
						for (std::uint8_t o = 0; o < field.OptionCount; o++) {
							known |= field.Options[o].Value;
						}
						if ((value & ~known) != 0) {
							fieldStats.NotAnOption++;
							fieldStats.Example = value;
						}
					}
				}

				bool uncovered = false;
				for (std::int32_t b = 0; b < Formats::LevelFile::EventParamsSize; b++) {
					std::uint8_t extra = std::uint8_t(event.Params[b] & ~covered[b]);
					if (extra != 0) {
						stats.UncoveredBits[b] |= extra;
						uncovered = true;
					}
				}
				if (uncovered) {
					stats.Uncovered++;
				}
			}

			void Report() const
			{
				std::int32_t unknownTypes = 0, issues = 0;
				for (std::size_t type = 1; type < Types.size(); type++) {
					const TypeStats& stats = Types[type];
					if (stats.Count == 0) {
						continue;
					}
					const EventDescriptor* descriptor = EventCatalog::Find(EventType(type));
					if (descriptor == nullptr) {
						LOGW("[Catalog scan] Unknown event type {} in {} events", type, stats.Count);
						unknownTypes++;
						continue;
					}
					if (stats.Uncovered > 0) {
						char mask[Formats::LevelFile::EventParamsSize * 3 + 1];
						for (std::int32_t b = 0; b < Formats::LevelFile::EventParamsSize; b++) {
							std::snprintf(&mask[b * 3], 4, "%02X ", stats.UncoveredBits[b]);
						}
						LOGW("[Catalog scan] {}: {} of {} events have bits no field takes: {}", descriptor->Key, stats.Uncovered, stats.Count, mask);
						issues++;
					}
					for (std::size_t f = 0; f < stats.Fields.size(); f++) {
						const FieldStats& fieldStats = stats.Fields[f];
						if (fieldStats.OutOfRange > 0) {
							LOGW("[Catalog scan] {}.{}: {} of {} events out of range, e.g. {}", descriptor->Key, descriptor->Fields[f].Name, fieldStats.OutOfRange, stats.Count, fieldStats.Example);
							issues++;
						}
						if (fieldStats.NotAnOption > 0) {
							LOGW("[Catalog scan] {}.{}: {} of {} events with a value that isn't an option, e.g. {}", descriptor->Key, descriptor->Fields[f].Name, fieldStats.NotAnOption, stats.Count, fieldStats.Example);
							issues++;
						}
					}
				}
				LOGI("Event catalog scan finished: {} unknown types, {} issues", unknownTypes, issues);
			}
		};

		// Tiles follow the extra tile sets as they're removed and resized, the overrides kept by tile ID too
		std::int32_t TestTileSetRemap()
		{
			std::int32_t problems = 0;
			auto expect = [&problems](bool condition, const char* what) {
				if (!condition) {
					LOGE("Tile set remap: {}", what);
					problems++;
				}
			};

			LevelDocument document;
			document.CreateNew("test"_s, 100, Vector2i(8, 8));
			for (std::uint16_t count : { std::uint16_t(10), std::uint16_t(20) }) {
				Formats::ExtraTileSetRecord tileSet = {};
				tileSet.Name = "extra"_s;
				tileSet.Count = count;
				document.ExtraTileSets.push_back(std::move(tileSet));
			}
			DocumentLayer& layer = document.Layers[0];
			layer.Tiles[0] = TileRef{ 5, 1, 0 };
			layer.Tiles[1] = TileRef{ 7, 2, Formats::TileRecord::FlipX };
			layer.Tiles[2] = TileRef{ 50, TileRef::DefaultTileSet, 0 };
			DocumentAnimation& animation = document.Animations.emplace_back();
			animation.Frames.push_back(TileRef{ 3, 2, 0 });
			Formats::TileDiffuseOverride& tileOverride = document.DiffuseOverrides.emplace_back();
			std::memset(&tileOverride, 0, sizeof(tileOverride));
			tileOverride.TileId = std::uint16_t(document.ToTileId(TileRef{ 7, 2, 0 }));
			expect(tileOverride.TileId == 117, "override before");

			// The first extra tile set is removed, the tiles of the second one move down
			LevelDocument::OverrideRefs overrides = document.CaptureOverrideRefs();
			document.ExtraTileSets.erase(document.ExtraTileSets.begin());
			Function<bool(TileRef&)> remove = [](TileRef& tile) {
				if (tile.Source == 1) {
					return false;
				}
				if (tile.Source > 1 && tile.Source <= TileRef::LastExtraTileSet) {
					tile.Source--;
				}
				return true;
			};
			std::int32_t cleared = document.RemapTiles(overrides, remove);
			expect(cleared == 1 && layer.Tiles[0].IsEmpty(), "removed tile set cleared");
			expect(layer.Tiles[1] == TileRef{ 7, 1, Formats::TileRecord::FlipX }, "tile moved down");
			expect(layer.Tiles[2] == TileRef{ 50, TileRef::DefaultTileSet, 0 }, "default tile kept");
			expect(document.Animations[0].Frames[0] == TileRef{ 3, 1, 0 }, "frame moved down");
			expect(document.DiffuseOverrides.size() == 1 && document.DiffuseOverrides[0].TileId == 107, "override moved down");

			// The tile set gets smaller, tiles past its end are gone
			overrides = document.CaptureOverrideRefs();
			document.ExtraTileSets[0].Count = 5;
			Function<bool(TileRef&)> keep = [](TileRef&) { return true; };
			cleared = document.RemapTiles(overrides, keep);
			expect(cleared == 1 && layer.Tiles[1].IsEmpty(), "tile past the end cleared");
			expect(document.Animations[0].Frames[0] == TileRef{ 3, 1, 0 }, "frame within the range kept");
			expect(document.DiffuseOverrides.empty(), "override past the end dropped");

			// Tile sets larger than the tile ID space push the animated tiles up
			document.DefaultTileCount = 4090;
			document.FitTileIdSpace();
			expect(document.GetAnimatedTilesOffset() >= document.GetStaticTileCount(), "tile ID space fits the tile sets");
			expect(document.Validate(), "document stays valid");

			if (problems == 0) {
				LOGI("Tile set remap test passed");
			}
			return problems;
		}

		// Images of odd sizes, indexed and RGBA, are read back as they were written
		std::int32_t TestPngCodec()
		{
			std::int32_t problems = 0;
			TestRandom random(1234);
			for (std::int32_t i = 0; i < 6; i++) {
				PngImage image;
				image.Create(1 + random.Next(70), 1 + random.Next(40), (i % 2) == 0);
				for (std::uint8_t& value : image.Pixels) {
					// Runs of the same value, as images have them, and noise
					value = std::uint8_t(random.Next(3) == 0 ? random.Next(256) : (&value == image.Pixels.data() ? 0 : *(&value - 1)));
				}
				if (image.Indexed) {
					image.PaletteSize = 1 + random.Next(256);
					for (std::int32_t j = 0; j < 256; j++) {
						image.Palette[j] = std::uint32_t(random.Next(256)) | (std::uint32_t(random.Next(256)) << 8) | (std::uint32_t(random.Next(256)) << 16) |
							(std::uint32_t(j < 3 ? random.Next(256) : 255) << 24);
					}
					for (std::uint8_t& value : image.Pixels) {
						value = std::uint8_t(value % image.PaletteSize);
					}
				}
				MemoryStream written;
				PngImage read;
				written.Seek(0, SeekOrigin::Begin);
				bool ok = PngCodec::Encode(written, image);
				written.Seek(0, SeekOrigin::Begin);
				ok = ok && PngCodec::Decode(written, read) && read.Width == image.Width && read.Height == image.Height && read.Indexed == image.Indexed &&
					read.Pixels.size() == image.Pixels.size() && std::memcmp(read.Pixels.data(), image.Pixels.data(), image.Pixels.size()) == 0;
				for (std::int32_t j = 0; ok && image.Indexed && j < image.PaletteSize; j++) {
					ok = (read.Palette[j] == image.Palette[j]);
				}
				if (!ok) {
					LOGE("PNG codec: image {} ({}x{}, {}) isn't read back as written", i, image.Width, image.Height, image.Indexed ? "indexed" : "RGBA");
					problems++;
				}
			}
			if (problems == 0) {
				LOGI("PNG codec test passed");
			}
			return problems;
		}

		// A tile set saved as images and made of them again is the same, with its masks
		std::int32_t TestTileSetImages()
		{
			std::int32_t problems = 0;
			TestRandom random(77);
			Formats::TileSetData tileSet;
			tileSet.Resize(7);
			for (std::int32_t i = 0; i < 256; i++) {
				tileSet.Palette[i] = std::uint32_t(random.Next(256)) | (std::uint32_t(random.Next(256)) << 8) | (std::uint32_t(random.Next(256)) << 16) | (i == 0 ? 0u : 0xFF000000u);
			}
			// The empty tile first and last, the others random
			for (std::int32_t tile = 1; tile <= 5; tile++) {
				std::uint8_t* image = &tileSet.Images[std::size_t(tile) * Formats::TileSetData::ImageBytesPerTile];
				std::uint8_t* mask = &tileSet.Masks[std::size_t(tile) * Formats::TileSetData::MaskBytesPerTile];
				for (std::int32_t p = 0; p < 32 * 32; p++) {
					image[p] = std::uint8_t(random.Next(4) == 0 ? 0 : 1 + random.Next(255));
				}
				for (std::int32_t b = 0; b < Formats::TileSetData::MaskBytesPerTile; b++) {
					mask[b] = std::uint8_t(random.Next(256));
				}
			}

			PngImage sheet = MakeTileSetSheet(tileSet, tileSet.Palette, 4);
			PngImage masks = MakeTileSetMaskSheet(tileSet, 4);
			TileImportOptions options;
			options.Colors = TileImportColors::OwnPalette;
			options.Masks = &masks;
			Formats::TileSetData imported;
			TileImportResult result;
			bool ok = SliceTileSet(sheet, options, imported, result) && sheet.Indexed;
			ok = ok && result.TileCount == 6 && !result.AddedEmptyTile && result.TrimmedTiles == 2 && !result.ColorsChanged;
			for (std::int32_t tile = 0; ok && tile < 6; tile++) {
				ok = (std::memcmp(&imported.Images[std::size_t(tile) * Formats::TileSetData::ImageBytesPerTile], &tileSet.Images[std::size_t(tile) * Formats::TileSetData::ImageBytesPerTile], 32 * 32) == 0 &&
					std::memcmp(&imported.Masks[std::size_t(tile) * Formats::TileSetData::MaskBytesPerTile], &tileSet.Masks[std::size_t(tile) * Formats::TileSetData::MaskBytesPerTile], Formats::TileSetData::MaskBytesPerTile) == 0 &&
					imported.TrueColor[tile] == 0);
			}
			for (std::int32_t i = 1; ok && i < 256; i++) {
				ok = (imported.Palette[i] == tileSet.Palette[i]);
			}
			if (!ok) {
				LOGE("Tile set images: the tile set isn't the same after it was saved as images and made of them again");
				problems++;
			} else {
				LOGI("Tile set images test passed");
			}
			return problems;
		}

		// A true color image reduced to a palette keeps the locked entries, looks close and leaves out the same tiles
		std::int32_t TestQuantizer()
		{
			std::int32_t problems = 0;
			auto expect = [&problems](bool condition, const char* what) {
				if (!condition) {
					LOGE("Quantizer: {}", what);
					problems++;
				}
			};

			// 5 tiles in a row: two gradients, the first one again, one of a locked color only, and an empty one
			PngImage image;
			image.Create(5 * 32, 32, false);
			auto setPixel = [&image](std::int32_t x, std::int32_t y, std::uint32_t color) {
				std::uint8_t* p = &image.Pixels[(std::size_t(y) * std::size_t(image.Width) + std::size_t(x)) * 4];
				p[0] = std::uint8_t(color); p[1] = std::uint8_t(color >> 8); p[2] = std::uint8_t(color >> 16); p[3] = std::uint8_t(color >> 24);
			};
			std::uint32_t palette[256] = {};
			bool locked[256] = {};
			TestRandom random(31);
			for (std::int32_t i = 1; i <= 100; i++) {
				palette[i] = std::uint32_t(random.Next(256)) | (std::uint32_t(random.Next(256)) << 8) | (std::uint32_t(random.Next(256)) << 16) | 0xFF000000u;
				locked[i] = true;
			}
			for (std::int32_t y = 0; y < 32; y++) {
				for (std::int32_t x = 0; x < 32; x++) {
					std::uint32_t first = std::uint32_t(x * 8) | (std::uint32_t(y * 8) << 8) | (64u << 16) | 0xFF000000u;
					std::uint32_t second = std::uint32_t(200) | (std::uint32_t(x * 4 + y * 4) << 8) | (std::uint32_t(255 - y * 8) << 16) | 0xFF000000u;
					setPixel(x, y, first);
					setPixel(32 + x, y, second);
					setPixel(64 + x, y, first);
					setPixel(96 + x, y, palette[42]);
				}
			}

			TileImportOptions options;
			options.Colors = TileImportColors::Reduce;
			options.Palette = palette;
			options.Locked = locked;
			options.SkipDuplicates = true;
			Formats::TileSetData tileSet;
			TileImportResult result;
			expect(SliceTileSet(image, options, tileSet, result), "sliced");
			// The empty tile put first, the repeated gradient left out, the empty one at the end trimmed
			expect(result.AddedEmptyTile && result.DuplicateTiles == 1 && result.TrimmedTiles == 1 && result.TileCount == 4, "tile count");
			expect(result.ColorsAdded > 0 && result.ColorsAdded <= 155, "colors added");
			bool keptLocked = true;
			for (std::int32_t i = 1; i <= 100; i++) {
				keptLocked &= (tileSet.Palette[i] == palette[i]);
			}
			expect(keptLocked, "locked entries kept");

			// The tile of a locked color is that entry, the gradients look close to what they were
			const std::uint8_t* lockedTile = &tileSet.Images[3 * Formats::TileSetData::ImageBytesPerTile];
			bool allLocked = true;
			for (std::int32_t p = 0; p < 32 * 32; p++) {
				allLocked &= (lockedTile[p] == 42);
			}
			expect(allLocked, "a locked color maps to its entry");
			float worst = 0.0f;
			for (std::int32_t tile = 1; tile <= 2; tile++) {
				const std::uint8_t* indices = &tileSet.Images[std::size_t(tile) * Formats::TileSetData::ImageBytesPerTile];
				for (std::int32_t p = 0; p < 32 * 32; p++) {
					OkLab original = OkLab::FromPaletteEntry(image.GetColor((tile - 1) * 32 + p % 32, p / 32));
					OkLab reduced = OkLab::FromPaletteEntry(tileSet.Palette[indices[p]]);
					worst = std::max(worst, original.DistanceSquared(reduced));
				}
			}
			expect(worst < 0.05f * 0.05f, "the reduced colors are close");

			// With dither, the same pixels still never become transparent and the locked tile stays as it is
			options.Dither = true;
			Formats::TileSetData dithered;
			expect(SliceTileSet(image, options, dithered, result), "sliced with dither");
			bool opaque = true;
			for (std::int32_t p = 0; p < 32 * 32; p++) {
				opaque &= (dithered.Images[1 * Formats::TileSetData::ImageBytesPerTile + p] != 0);
			}
			expect(opaque, "dithered pixels stay opaque");

			if (problems == 0) {
				LOGI("Quantizer test passed (colors added: {}, worst distance: {:.4f})", result.ColorsAdded, std::sqrt(worst));
			}
			return problems;
		}

		// Every effect makes tiles that do something, true color and without collision, and frosted glass blurs fully
		std::int32_t TestFxTiles()
		{
			std::int32_t problems = 0;
			for (std::int32_t k = 0; k < std::int32_t(FxTileKind::Count); k++) {
				Formats::TileSetData tiles;
				GenerateFxTiles(FxTileKind(k), 3, 2, 1.0f, tiles);
				std::int32_t active = 0, solid = 0, minBlur = 255;
				for (std::int32_t t = 0; t < tiles.TileCount; t++) {
					solid += (tiles.TrueColor[t] == 0 ? 1000 : 0);
					for (std::int32_t i = 0; i < Formats::TileSetData::MaskBytesPerTile; i++) {
						solid += (tiles.Masks[std::size_t(t) * Formats::TileSetData::MaskBytesPerTile + i] != 0 ? 1 : 0);
					}
					for (std::int32_t i = 0; i < 32 * 32; i++) {
						std::uint8_t alpha = tiles.Images[std::size_t(t) * Formats::TileSetData::ImageBytesPerTile + i * 4 + 3];
						if (alpha != 0) {
							active++;
							minBlur = std::min<std::int32_t>(minBlur, alpha);
						}
					}
				}
				bool frostedOk = (FxTileKind(k) != FxTileKind::FrostedGlass || minBlur >= 200);
				if (tiles.TileCount != 6 || active == 0 || solid != 0 || !frostedOk) {
					LOGE("FX tiles: effect {} made {} tiles, {} texels with an effect, {} solid, the least blur {}", k, tiles.TileCount, active, solid, minBlur);
					problems++;
				}
			}
			if (problems == 0) {
				LOGI("FX tiles test passed");
			}
			return problems;
		}

		// A palette saved as a file of another program and read back is the same, in each of the formats
		std::int32_t TestViewState()
		{
			LevelDocument document;
			document.CreateNew("test"_s, 100, Vector2i(32, 16));
			DocumentViewState state;
			state.HasCamera = true;
			state.CameraCenter = Vector2f(123.5f, -40.25f);
			state.CameraZoom = 0.375f;
			state.CurrentLayer = 5;
			state.ShowGrid = false;
			state.ShowEvents = true;
			state.ShowPlayPath = false;
			state.FlatLayers = true;
			state.MaskMode = 2;
			state.PickerPanel = 1;
			state.HasPlaySettings = true;
			state.PlayCharacter = 3;
			state.PlayDifficulty = 1;
			state.PlayGameMode = 4;
			state.PlayPlayerCount = 3;
			state.HasPlayStart = true;
			state.PlayStart = Vector2i(-17, 99999);
			state.Write(document);

			// Through a file, so the chunk is written and read as any other
			MemoryStream stream;
			DocumentViewState read;
			LevelDocument loaded;
			bool ok = document.Save(stream, false, 1);
			stream.Seek(0, SeekOrigin::Begin);
			ok = ok && loaded.Load(stream, nullptr) && DocumentViewState::Read(loaded, read);
			ok = ok && read.HasCamera && read.CameraCenter == state.CameraCenter && read.CameraZoom == state.CameraZoom && read.CurrentLayer == 5 &&
				!read.ShowGrid && read.ShowEvents && !read.ShowPlayPath && read.FlatLayers && read.MaskMode == 2 && read.PickerPanel == 1 &&
				read.HasPlaySettings && read.PlayCharacter == 3 && read.PlayDifficulty == 1 && read.PlayGameMode == 4 && read.PlayPlayerCount == 3 &&
				read.HasPlayStart && read.PlayStart == state.PlayStart;

			// Export leaves it out
			MemoryStream exported;
			LevelDocument exportedLoaded;
			ok = ok && document.Save(exported, true, 1);
			exported.Seek(0, SeekOrigin::Begin);
			ok = ok && exportedLoaded.Load(exported, nullptr) && !DocumentViewState::Read(exportedLoaded, read);

			if (!ok) {
				LOGE("View state: the edoc chunk isn't the same after it was saved and loaded, or export kept it");
				return 1;
			}
			return 0;
		}

		std::int32_t TestTileSetOrder()
		{
			LevelDocument document;
			document.CreateNew("test"_s, 6, Vector2i(4, 1));
			std::int32_t sprite = document.GetSpriteLayerIndex();
			auto& tiles = document.Layers[sprite].Tiles;
			for (std::int32_t i = 0; i < 4; i++) {
				tiles[i] = TileRef{ std::uint16_t(i + 1), TileRef::DefaultTileSet, std::uint8_t(i == 2 ? Formats::TileRecord::FlipX : 0) };
			}
			// 1 and 2 swap, an empty tile goes before 3, 4 is gone, 5 stays at the end
			const std::int32_t order[] = { 0, 2, 1, -1, 3, 5 };
			std::int32_t cleared = RemapLevelToTileSetOrder(document, order);
			bool ok = (cleared == 1 && document.DefaultTileCount == 6 &&
				tiles[0] == TileRef{ 2, TileRef::DefaultTileSet, 0 } && tiles[1] == TileRef{ 1, TileRef::DefaultTileSet, 0 } &&
				tiles[2] == TileRef{ 4, TileRef::DefaultTileSet, Formats::TileRecord::FlipX } && tiles[3].IsEmpty());
			if (!ok) {
				LOGE("Tile set order: tiles of the level don't follow the new order of their tile set");
				return 1;
			}
			return 0;
		}

		std::int32_t TestPaletteFiles()
		{
			std::int32_t problems = 0;
			std::uint32_t palette[256];
			TestRandom random(57);
			for (std::int32_t i = 0; i < 256; i++) {
				palette[i] = std::uint32_t(random.Next(256)) | (std::uint32_t(random.Next(256)) << 8) | (std::uint32_t(random.Next(256)) << 16) | 0xFF000000u;
			}
			for (StringView extension : { "pal"_s, "gpl"_s, "act"_s, "png"_s }) {
				String path = fs::CombinePath(fs::GetWorkingDirectory(), String("selftest-palette."_s + extension));
				std::uint32_t read[256] = {};
				bool written = PaletteFile::Write(path, palette, "Test"_s);
				std::int32_t count = (written ? PaletteFile::Read(path, read) : 0);
				fs::RemoveFile(path);
				if (!written || count != 256 || std::memcmp(read, palette, sizeof(palette)) != 0) {
					LOGE("Palette files: a .{} palette isn't the same after it was written and read ({} colors read)", extension, count);
					problems++;
				}
			}
			{
				// A true color image of a grid of the colors, as other programs show palettes
				PngImage image;
				image.Create(48, 48, false);
				for (std::int32_t y = 0; y < image.Height; y++) {
					for (std::int32_t x = 0; x < image.Width; x++) {
						std::uint32_t color = palette[(y / 3) * 16 + x / 3];
						std::uint8_t* pixel = &image.Pixels[(y * image.Width + x) * 4];
						pixel[0] = std::uint8_t(color); pixel[1] = std::uint8_t(color >> 8); pixel[2] = std::uint8_t(color >> 16); pixel[3] = 255;
					}
				}
				String path = fs::CombinePath(fs::GetWorkingDirectory(), "selftest-palette-grid.png"_s);
				bool written = false;
				{
					auto s = fs::Open(path, FileAccess::Write);
					written = (s->IsValid() && PngCodec::Encode(*s, image));
				}
				std::uint32_t read[256] = {};
				std::int32_t count = (written ? PaletteFile::Read(path, read) : 0);
				fs::RemoveFile(path);
				if (count != 256 || std::memcmp(read, palette, sizeof(palette)) != 0) {
					LOGE("Palette files: the colors of a grid in a true color image weren't read ({} colors read)", count);
					problems++;
				}
			}
			if (problems == 0) {
				LOGI("Palette files test passed");
			}
			return problems;
		}

		// Polylines longer than a command can draw are split into commands that each fit, and none of their points is lost
		std::int32_t TestPolylineSplit()
		{
			std::int32_t problems = 0;
			const std::int32_t maxVertices = Rendering::PolylineRenderer::GetMaxVerticesPerCommand();
			Rendering::PolylineRenderer renderer(1 << 22);
			renderer.BeginFrame();

			SmallVector<Vector2f, 0> points;
			SmallVector<float, 0> halfThicknesses;
			for (std::int32_t i = 0; i < 70000; i++) {
				points.push_back(Vector2f(float(i % 1000), float(i / 1000)));
				halfThicknesses.push_back(2.0f);
			}
			SmallVector<RenderCommand*, 8> commands;
			renderer.DrawRange(points, 0, std::int32_t(points.size()) - 1, halfThicknesses, 0, Colorf::White, commands);
			std::int32_t vertices = 0;
			bool fits = true;
			for (RenderCommand* command : commands) {
				std::int32_t count = std::int32_t(command->GetGeometry().GetVertexCount());
				fits &= (count <= maxVertices);
				vertices += count;
			}
			// Two vertices a point, the parts share a point with the one before them
			std::int32_t expected = (std::int32_t(points.size()) + std::int32_t(commands.size()) - 1) * 2;
			if (commands.size() < 2 || !fits || vertices != expected) {
				LOGE("Polyline split: a long polyline became {} commands with {} vertices, {} expected, at most {} each", commands.size(), vertices, expected, maxVertices);
				problems++;
			}

			// Many short ones go together, a long one among them on its own
			SmallVector<std::int32_t, 0> ends;
			SmallVector<Vector2f, 0> many;
			for (std::int32_t p = 0; p < 4000; p++) {
				std::int32_t count = (p == 2000 ? 40000 : 10);
				for (std::int32_t i = 0; i < count; i++) {
					many.push_back(Vector2f(float(i), float(p)));
				}
				ends.push_back(std::int32_t(many.size()));
			}
			commands.clear();
			renderer.DrawMany(many, ends, 1.0f, 0, Colorf::White, commands);
			fits = true;
			vertices = 0;
			for (RenderCommand* command : commands) {
				std::int32_t count = std::int32_t(command->GetGeometry().GetVertexCount());
				fits &= (count <= maxVertices);
				vertices += count;
			}
			if (commands.size() < 3 || !fits || vertices < std::int32_t(many.size()) * 2) {
				LOGE("Polyline split: many polylines became {} commands with {} vertices, at most {} each", commands.size(), vertices, maxVertices);
				problems++;
			}
			if (problems == 0) {
				LOGI("Polyline split test passed ({} vertices at most a command)", maxVertices);
			}
			return problems;
		}

		// Tiles and events keep their place when a layer grows or shrinks from a side
		std::int32_t TestLayerResize()
		{
			std::int32_t problems = 0;
			auto expect = [&problems](bool condition, const char* what) {
				if (!condition) {
					LOGE("Layer resize: {}", what);
					problems++;
				}
			};

			LevelDocument document;
			document.CreateNew("test"_s, 100, Vector2i(4, 3));
			std::int32_t sprite = document.GetSpriteLayerIndex();
			DocumentLayer& layer = document.Layers[sprite];
			layer.GetTile(0, 0) = TileRef{ 5, TileRef::DefaultTileSet, 0 };
			layer.GetTile(3, 2) = TileRef{ 7, TileRef::DefaultTileSet, 0 };
			Formats::EventRecord event = LevelDocument::GetEmptyEvent();
			event.Type = 1;
			document.SetEvent(3, 2, event);
			document.Properties.WaterLevel = 64;

			// Grows to the left and up by 2 and 1, the old tiles at the right bottom
			expect(document.ResizeLayer(sprite, Vector2i(6, 4), Vector2i(2, 1), LevelDocument::ResizeFill::ExtendEdges), "resized");
			DocumentLayer& resized = document.Layers[sprite];
			expect(resized.GetWidth() == 6 && resized.GetHeight() == 4, "new size");
			expect(resized.GetTile(2, 1) == TileRef{ 5, TileRef::DefaultTileSet, 0 }, "tile moved");
			expect(resized.GetTile(0, 0) == TileRef{ 5, TileRef::DefaultTileSet, 0 }, "corner extended");
			expect(resized.GetTile(5, 3) == TileRef{ 7, TileRef::DefaultTileSet, 0 }, "far tile moved");
			expect(document.GetEvent(5, 3).Type == 1 && document.Events.size() == 1, "event moved");
			expect(document.Properties.WaterLevel == 96, "water moved");

			// Shrinks from the right, the event falls off
			expect(document.ResizeLayer(sprite, Vector2i(4, 4), Vector2i(0, 0), LevelDocument::ResizeFill::Empty), "shrunk");
			expect(document.Events.empty(), "event outside dropped");
			expect(!document.ResizeLayer(sprite, Vector2i(0, 4), Vector2i(0, 0), LevelDocument::ResizeFill::Empty), "empty size refused");
			expect(document.Validate(), "document stays valid");

			if (problems == 0) {
				LOGI("Layer resize test passed");
			}
			return problems;
		}

		TestResult TestLevel(StringView path, ITileSetLibrary& library, std::uint32_t seed, CatalogScan& scan)
		{
			auto s = fs::Open(path, FileAccess::Read);
			if (!s->IsValid()) {
				LOGE("[{}] Cannot be opened", path);
				return TestResult::Failed;
			}
			std::int64_t fileSize = s->GetSize();
			SmallVector<std::uint8_t, 0> original;
			original.resize_for_overwrite(std::size_t(fileSize));
			if (s->Read(original.data(), fileSize) != fileSize) {
				LOGE("[{}] Cannot be read", path);
				return TestResult::Failed;
			}
			s = nullptr;

			MemoryStream input(original.data(), std::int64_t(original.size()));
			Formats::LevelHeader header;
			if (!Formats::LevelFile::ReadHeader(input, header)) {
				// An original JJ2 level shares the extension
				return TestResult::NotNative;
			}

			LevelDocument document;
			input.Seek(0, SeekOrigin::Begin);
			if (!document.Load(input, &library)) {
				LOGE("[{}] Cannot be loaded", path);
				return TestResult::Failed;
			}

			String problem;
			if (!document.Validate(&problem)) {
				LOGW("[{}] Document is not valid: {}", path, problem);
			}

			for (const DocumentEvent& event : document.Events) {
				scan.Add(event.Record);
			}
			for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
				scan.Add(event.Event);
			}

			{
				// Check Level runs on every level, what it finds in the original ones is mostly how they're made
				SmallVector<LevelProblem, 0> problems;
				CheckLevel(document, problems);
				std::int32_t errors = std::int32_t(std::count_if(problems.begin(), problems.end(), [](const LevelProblem& p) { return p.Severity == LevelProblemSeverity::Error; }));
				if (errors > 0) {
					LOGD("[{}] Check Level: {} errors, the first: {}", path, errors, problems[0].Text);
				}
			}

			MemoryStream exported;
			if (!Export(document, exported)) {
				LOGE("[{}] Cannot be saved", path);
				return TestResult::Failed;
			}

			TestResult result = TestResult::Identical;
			if (!AreEqual(exported, original)) {
				// Written by another tool, it has to be stable at least
				LevelDocument again;
				exported.Seek(0, SeekOrigin::Begin);
				MemoryStream exportedAgain;
				if (!again.Load(exported, &library) || !Export(again, exportedAgain) || !AreEqual(exportedAgain, exported)) {
					LOGE("[{}] Level differs after it was saved", path);
					return TestResult::Failed;
				}
				LOGW("[{}] Level is the same, but written differently", path);
				result = TestResult::Equivalent;
			}

			if (!TestEditorData(path, document, exported, library) || !TestCommands(path, document, exported, seed)) {
				return TestResult::Failed;
			}
			return result;
		}
	}

	bool EditorSelfTest::Run(StringView directory)
	{
		String path = directory;
		if (path.empty()) {
			path = fs::CombinePath({ ContentResolver::Get().GetCachePath(), "Episodes"_s });
		}
		if (!fs::DirectoryExists(path)) {
			LOGE("Directory \"{}\" doesn't exist", path);
			return false;
		}

		SmallVector<String, 0> files;
		CollectFiles(path, files);
		LOGI("Testing {} levels in \"{}\"", files.size(), path);

		std::int32_t catalogProblems = TestCatalogConsistency();
		catalogProblems += TestTileSetRemap();
		catalogProblems += TestLayerResize();
		catalogProblems += TestPngCodec();
		catalogProblems += TestTileSetImages();
		catalogProblems += TestQuantizer();
		catalogProblems += TestPolylineSplit();
		catalogProblems += TestPaletteFiles();
		catalogProblems += TestFxTiles();
		catalogProblems += TestViewState();
		catalogProblems += TestTileSetOrder();

		ContentTileSetLibrary library;
		CatalogScan scan;
		std::int32_t identical = 0, equivalent = 0, notNative = 0, failed = 0;
		std::uint32_t seed = 1;
		for (const String& file : files) {
			switch (TestLevel(file, library, seed++, scan)) {
				case TestResult::Identical: identical++; break;
				case TestResult::Equivalent: equivalent++; break;
				case TestResult::NotNative: notNative++; break;
				default: failed++; break;
			}
		}
		scan.Report();

		LOGI("Editor self-test finished: {} identical, {} written differently, {} not native, {} failed", identical, equivalent, notNative, failed);
		return (failed == 0 && catalogProblems == 0);
	}
}

#endif
