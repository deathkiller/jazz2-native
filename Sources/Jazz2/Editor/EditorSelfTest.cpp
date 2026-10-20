#include "EditorSelfTest.h"

#if defined(WITH_EDITOR) && defined(DEATH_DEBUG)

#include "Document/Annotations.h"
#include "Document/CommandArchive.h"
#include "Document/Commands.h"
#include "Document/DocumentViewState.h"
#include "Document/TerrainSplines.h"
#include "Events/EventCatalog.h"
#include "Events/EventGizmos.h"
#include "Events/EventPreviews.h"
#include "Files/AutosaveService.h"
#include "Files/ContentTileSetLibrary.h"
#include "Files/EpisodeFile.h"
#include "Files/PoFile.h"
#include "Files/ZipWriter.h"
#include "../Formats/LevelTextsChunk.h"
#include "Imaging/PaletteFile.h"
#include "Imaging/PaletteMatch.h"
#include "Imaging/PngCodec.h"
#include "Imaging/TileImages.h"
#include "Input/ActionRegistry.h"
#include "Runtime/PlayPathHistory.h"
#include "Shell/FxTileGenerator.h"
#include "Shell/LevelCheck.h"
#include "Shell/TileSetLayoutDialog.h"
#include "Tests/ToolTests.h"
#include "Tools/ToolActions.h"
#include "../Formats/TileSetFile.h"
#include "../Formats/VarUint.h"
#include "../ContentResolver.h"
#include "../Rendering/PolylineRenderer.h"
#include "../Tiles/LayerTypes.h"

#include <Containers/String.h>
#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>
#include <IO/MemoryStream.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

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
				// Any tile of the tile set, the last ones are taken by the animated tiles in a level that has as many tile IDs
				// as the tile set has tiles, as in the original game, those make the tile ID space grow when they're placed
				// by ExecuteGrowingTileIdSpace(), a tile there would be saved as an animated one otherwise
				std::int32_t tileCount = (document.DefaultTileCount > 0 ? document.DefaultTileCount : 256);
				tile.Index = std::uint16_t(random.Next(tileCount));
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
				// As the editor does, the tiles past the tile ID space make it grow
				ExecuteGrowingTileIdSpace(history, Death::move(command), changes);
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
				history.Execute(Death::move(command), changes);
			} else if (kind < 65) {
				Formats::LayerRecord record = document.Layers[layerIndex].Record;
				record.OffsetX += float(random.Next(64));
				record.SpeedY = float(random.Next(100)) / 50.0f;
				record.Flags ^= Formats::LayerRecord::RepeatX;
				auto command = std::make_unique<SetLayerPropertiesCommand>(document, layerIndex, record,
					random.Next(2) == 0 ? "Renamed"_s : StringView(document.Layers[layerIndex].Name), "Layer properties"_s);
				history.Execute(Death::move(command), changes, random.Next(2) == 0);
			} else if (kind < 72) {
				Formats::LevelProperties properties = document.Properties;
				properties.DisplayName = properties.DisplayName + "!"_s;
				properties.WaterLevel = std::uint16_t(random.Next(UINT16_MAX));
				auto command = std::make_unique<SetLevelPropertiesCommand>(document, document.Flags ^ LevelFlags::HasVerticalSplitscreen, properties, "Level properties"_s);
				history.Execute(Death::move(command), changes, random.Next(2) == 0);
			} else if (kind < 78) {
				// Add, remove or move a layer by the commands that keep only the layer
				std::int32_t layerCount = std::int32_t(document.Layers.size());
				std::int32_t variant = random.Next(3);
				if (variant == 0 && layerCount < UINT8_MAX) {
					DocumentLayer layer;
					layer.Record = document.Layers[layerIndex].Record;
					layer.Record.Kind = Formats::LayerKind::Other;
					layer.Record.Width = 1 + random.Next(30);
					layer.Record.Height = 1 + random.Next(30);
					layer.Tiles.resize(std::size_t(layer.Record.Width) * std::size_t(layer.Record.Height), TileRef{});
					layer.Tiles[0] = MakeRandomTile(random, document);
					ExecuteGrowingTileIdSpace(history, InsertLayerCommand::Add(document, random.Next(layerCount + 1), layer, "Add layer"_s), changes);
				} else if (variant == 1 && !document.Layers[layerIndex].IsSpriteLayer()) {
					history.Execute(InsertLayerCommand::Remove(document, layerIndex, "Remove layer"_s), changes);
				} else if (layerCount > 1) {
					std::int32_t to = random.Next(layerCount);
					if (to != layerIndex) {
						history.Execute(std::make_unique<MoveLayerCommand>(layerIndex, to, "Move layer"_s), changes);
					}
				}
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
					layer.Tiles = Death::move(tiles);
					layer.Record.Width = newWidth;
					layer.Record.Height = newHeight;
				} else if (document.Layers.size() < UINT8_MAX) {
					DocumentLayer layer;
					layer.Record = document.Layers[layerIndex].Record;
					layer.Record.Kind = Formats::LayerKind::Other;
					layer.Record.Width = 1 + random.Next(30);
					layer.Record.Height = 1 + random.Next(30);
					layer.Tiles.resize(std::size_t(layer.Record.Width) * std::size_t(layer.Record.Height), TileRef{});
					document.Layers.insert(document.Layers.begin() + random.Next(std::int32_t(document.Layers.size()) + 1), Death::move(layer));
				}
				command->End(document, changes);
				history.Record(Death::move(command));
			} else if (kind < 90) {
				history.BeginTransaction("Transaction"_s);
				for (std::int32_t i = 0; i < 3; i++) {
					std::int32_t index = random.Next(std::int32_t(document.Layers.size()));
					if (!document.Layers[index].Tiles.empty()) {
						auto command = std::make_unique<SetTilesCommand>(document, index, "Tiles"_s);
						FillTiles(random, document, *command, index);
						ExecuteGrowingTileIdSpace(history, Death::move(command), changes);
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
					// Texts and the parameters of an event inside take whole bytes, not a number
					bool isText = (field.Type == ParamType::String || field.Type == ParamType::Raw || field.Type == ParamType::Contents);
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

		// Mirroring, swapping teams, the events inside crates, fields shown by others and scaled values of the catalog
		std::int32_t TestCatalogParams()
		{
			std::int32_t problems = 0;
			auto expect = [&problems](bool condition, const char* what) {
				if (!condition) {
					LOGE("[Catalog params] {}", what);
					problems++;
				}
			};

			// A spring launching to the right launches to the left mirrored, one launching up launches down
			const EventDescriptor* spring = EventCatalog::Find(EventType::Spring);
			Formats::EventRecord event = EventCatalog::MakeDefault(EventType::Spring);
			event.Params[1] = 1;
			expect(EventCatalog::MirrorParams(spring, event, true) && event.Params[1] == 3, "spring mirrored horizontally");
			expect(EventCatalog::MirrorParams(spring, event, true) && event.Params[1] == 1, "spring mirrored back");
			event.Params[1] = 0;
			expect(EventCatalog::MirrorParams(spring, event, false) && event.Params[1] == 2, "spring mirrored vertically");

			// Forces to the left and to the right swap with each other, both at once
			const EventDescriptor* force = EventCatalog::Find(EventType::AreaHForce);
			event = EventCatalog::MakeDefault(EventType::AreaHForce);
			event.Params[0] = 3;
			event.Params[1] = 5;
			EventCatalog::MirrorParams(force, event, true);
			expect(event.Params[0] == 5 && event.Params[1] == 3, "belts swapped");

			// A crate gets a spring inside with the parameters of a new spring, which is mirrored with the crate
			const EventDescriptor* crate = EventCatalog::Find(EventType::Crate);
			event = EventCatalog::MakeDefault(EventType::Crate);
			const EventParamField* contents = nullptr;
			for (std::uint8_t i = 0; crate != nullptr && i < crate->FieldCount; i++) {
				if (crate->Fields[i].Type == ParamType::Contents) {
					contents = &crate->Fields[i];
				}
			}
			expect(contents != nullptr, "crate has contents");
			if (crate != nullptr && contents != nullptr) {
				EventCatalog::SetContentsType(*crate, crate->Fields[contents->ContentsTypeField], event, std::uint16_t(EventType::Spring));
				Formats::EventRecord inside = EventCatalog::GetContents(*crate, *contents, event);
				expect(inside.Type == std::uint16_t(EventType::Spring) && (event.Params[contents->Offset + 2] & 0x01) != 0, "contents reset to a new spring");
				expect(EventCatalog::GetContentsDescriptor(*crate, *contents, event) == spring, "contents described");
				event.Params[contents->Offset + 1] = 1;
				EventCatalog::MirrorParams(crate, event, true);
				expect(event.Params[contents->Offset + 1] == 3, "contents mirrored");
				// The same type again keeps what's set
				EventCatalog::SetContentsType(*crate, crate->Fields[contents->ContentsTypeField], event, std::uint16_t(EventType::Spring));
				expect(event.Params[contents->Offset + 1] == 3, "contents kept for the same type");
			}

			// Teams swapped in the fields marked as teams
			const EventDescriptor* base = EventCatalog::Find(EventType::CtfBase);
			event = EventCatalog::MakeDefault(EventType::CtfBase);
			event.Params[0] = 0;
			expect(EventCatalog::SwapTeamParams(base, event.Params, 0, 1) && event.Params[0] == 1, "teams swapped");

			// "Turn On" of a trigger crate is ignored while it toggles, it isn't shown then
			const EventDescriptor* triggerCrate = EventCatalog::Find(EventType::TriggerCrate);
			event = EventCatalog::MakeDefault(EventType::TriggerCrate);
			if (triggerCrate != nullptr && triggerCrate->FieldCount >= 3) {
				expect(EventCatalog::IsFieldVisible(*triggerCrate, 1, event), "turn on shown");
				event.Params[2] = 1;
				expect(!EventCatalog::IsFieldVisible(*triggerCrate, 1, event), "turn on hidden while toggling");
			}

			// A bridge is shown in pixels, stored in steps of 16 of them
			const EventDescriptor* bridge = EventCatalog::Find(EventType::Bridge);
			if (bridge != nullptr && bridge->FieldCount > 0) {
				const EventParamField& width = bridge->Fields[0];
				expect(EventCatalog::ToDisplayValue(width, 8) == 128 && EventCatalog::FromDisplayValue(width, 130) == 8 &&
					EventCatalog::FromDisplayValue(width, 136) == 9, "scaled values");
			}

			// Links: a warp of two tiles next to each other to its target, a warp to nowhere, a rock trigger to its rock
			LevelDocument document;
			document.CreateNew("test"_s, 100, Vector2i(20, 10));
			auto place = [&document](std::int32_t x, std::int32_t y, EventType type, std::uint8_t id) {
				Formats::EventRecord placed = EventCatalog::MakeDefault(type);
				placed.Params[0] = id;
				document.SetEvent(x, y, placed);
			};
			place(2, 2, EventType::WarpOrigin, 3);
			place(3, 2, EventType::WarpOrigin, 3);
			place(10, 5, EventType::WarpTarget, 3);
			place(1, 8, EventType::WarpOrigin, 7);
			place(5, 5, EventType::RollingRockTrigger, 1);
			place(6, 7, EventType::RollingRock, 1);
			SmallVector<EventLink, 0> links;
			EventPreviews::CollectLinks(document, links);
			std::int32_t warpLinks = 0, missingLinks = 0, rockLinks = 0;
			for (const EventLink& link : links) {
				if (link.Missing) {
					missingLinks++;
				} else if (StringView(link.Channel) == "warp"_s) {
					warpLinks++;
					expect(link.From.W == 64.0f && link.From.H == 32.0f && link.To.X == 320.0f && link.To.Y == 160.0f, "warp link ends");
				} else if (StringView(link.Channel) == "rollingRock"_s) {
					rockLinks++;
				}
			}
			expect(warpLinks == 1 && missingLinks == 1 && rockLinks == 1 && links.size() == 3, "links found");
			std::uint32_t onlyCell = 1 + 8 * 20;
			EventPreviews::CollectLinks(document, links, ArrayView<const std::uint32_t>(&onlyCell, 1));
			expect(links.size() == 1 && links[0].Missing, "links of a tile");

			// The track of a race: 1 to 2, whose two tiles are one, then 4 is after a skipped order, so only it leads to the
			// exit, as the only waypoint of another group does; the warps don't count a lap without Set Lap
			auto placeWaypoint = [&document](std::int32_t x, std::int32_t y, std::uint8_t order, std::uint8_t group) {
				Formats::EventRecord placed = EventCatalog::MakeDefault(EventType::RaceCheckpoint);
				placed.Params[0] = order;
				placed.Params[1] = group;
				document.SetEvent(x, y, placed);
			};
			placeWaypoint(12, 1, 1, 0);
			placeWaypoint(14, 1, 2, 0);
			placeWaypoint(14, 3, 2, 0);
			placeWaypoint(16, 6, 4, 0);
			placeWaypoint(12, 8, 0, 1);
			document.SetEvent(18, 8, EventCatalog::MakeDefault(EventType::AreaEndOfLevel));
			EventPreviews::CollectRaceTrack(document, links);
			expect(links.size() == 3 && links[0].From.X == 12 * 32.0f && links[0].To.Y == 32.0f && links[0].To.H == 96.0f &&
				links[1].From.Y == 6 * 32.0f && links[1].To.X == 18 * 32.0f && links[2].Id == 1 && links[2].To.X == 18 * 32.0f, "race track");

			// Arrows: a belt of 5 to the right and 3 to the left is one of 2 to the right, a wind of its own
			event = EventCatalog::MakeDefault(EventType::AreaHForce);
			event.Params[0] = 3;
			event.Params[1] = 5;
			event.Params[5] = 4;
			EventDirection directions[4];
			std::int32_t count = EventPreviews::CollectDirections(event, directions);
			expect(count == 2 && directions[0].Group == 0 && directions[0].Vector.X == 2.0f && directions[1].Group == 2 && directions[1].Vector.X == 4.0f, "arrows");

			// Arrows dragged by their tips: a tube turned to go down, a belt to the left with its two fields, a handle dragged
			// into the event keeps a step the way it's dragged
			{
				Vector2i cell(4, 4);
				Vector2f center(4 * 32.0f + 16.0f, 4 * 32.0f + 16.0f);
				SmallVector<EventGizmo, 4> gizmos;
				auto findDirection = [&gizmos]() -> const EventGizmo* {
					for (const EventGizmo& gizmo : gizmos) {
						if (gizmo.Kind == EventGizmoKind::Direction) {
							return &gizmo;
						}
					}
					return nullptr;
				};
				event = EventCatalog::MakeDefault(EventType::ModifierTube);
				EventGizmos::Collect(event, cell, 640, gizmos);
				const EventGizmo* tube = findDirection();
				expect(tube != nullptr && std::abs(tube->World.X - center.X - EventPreviews::GetDirectionLength(8.0f)) < 0.01f && tube->World.Y == center.Y, "tube handle at the tip");
				if (tube != nullptr) {
					EventGizmo tubeGizmo = *tube;
					// A tube goes any way
					Vector2f tubeDrag = EventGizmos::GetDragDirection(event, tubeGizmo);
					expect(tubeDrag.X == 0.0f && tubeDrag.Y == 0.0f, "tube drag direction");
					expect(EventGizmos::Apply(event, cell, 640, tubeGizmo, center + Vector2f(0.0f, EventPreviews::GetDirectionLength(5.0f))) &&
						std::int8_t(event.Params[0]) == 0 && std::int8_t(event.Params[1]) == 5, "tube dragged down");
					EventGizmos::Apply(event, cell, 640, tubeGizmo, center + Vector2f(-2.0f, 0.0f));
					expect(std::int8_t(event.Params[0]) == -1 && event.Params[1] == 0, "tube dragged into the event");
					// Snapped, almost diagonally is diagonally with the same value on both axes, almost up is up
					EventGizmos::Apply(event, cell, 640, tubeGizmo, center + Vector2f(50.0f, 38.0f), true);
					expect(std::int8_t(event.Params[0]) > 0 && event.Params[0] == event.Params[1], "tube snapped diagonally");
					EventGizmos::Apply(event, cell, 640, tubeGizmo, center + Vector2f(9.0f, -60.0f), true);
					expect(event.Params[0] == 0 && std::int8_t(event.Params[1]) < 0, "tube snapped up");
				}
				event = EventCatalog::MakeDefault(EventType::AreaHForce);
				gizmos.clear();
				EventGizmos::Collect(event, cell, 640, gizmos);
				const EventGizmo* belt = findDirection();
				expect(belt != nullptr && belt->Field == 0 && belt->World.X > center.X, "belt handle");
				if (belt != nullptr) {
					EventGizmo beltGizmo = *belt;
					expect(EventGizmos::Apply(event, cell, 640, beltGizmo, center + Vector2f(-EventPreviews::GetDirectionLength(3.0f), 20.0f)) &&
						event.Params[0] == 3 && event.Params[1] == 0, "belt dragged to the left");
					// A belt goes only left or right
					Vector2f beltDrag = EventGizmos::GetDragDirection(event, beltGizmo);
					expect(beltDrag.X == 1.0f && beltDrag.Y == 0.0f, "belt drag direction");
				}
				expect(std::abs(EventPreviews::GetDirectionStrength(EventPreviews::GetDirectionLength(37.0f)) - 37.0f) < 0.01f, "arrow length inverted");

				// Each handle is where its arrow ends, the arrows start at the position of the event
				event = EventCatalog::MakeDefault(EventType::AreaHForce);
				event.Params[5] = 9;
				gizmos.clear();
				EventGizmos::Collect(event, cell, 640, gizmos);
				count = EventPreviews::CollectDirections(event, directions);
				std::int32_t atTips = 0;
				for (const EventGizmo& gizmo : gizmos) {
					for (std::int32_t d = 0; d < count; d++) {
						if (gizmo.Kind == EventGizmoKind::Direction && (gizmo.World - EventPreviews::GetDirectionTip(center, directions[d].Vector)).Length() < 0.01f) {
							atTips++;
						}
					}
				}
				expect(count == 2 && atTips == 2, "handles at the tips of the arrows");
			}

			// An exit without a text goes to the next level of the properties of the level, a bonus exit to the secret one
			{
				const EventDescriptor* exit = EventCatalog::Find(EventType::AreaEndOfLevel);
				event = EventCatalog::MakeDefault(EventType::AreaEndOfLevel);
				document.Properties.NextLevel = "castle1n"_s;
				document.Properties.SecretLevel = "secret"_s;
				String defaultLevel;
				expect(exit != nullptr && exit->FieldCount > 3 && EventPreviews::DescribeDefaultLevel(document, event, exit->Fields[3], defaultLevel) &&
					defaultLevel.contains("castle1n"_s), "default next level");
				if (exit != nullptr && exit->FieldCount > 3) {
					event.Params[0] = 3;
					expect(EventPreviews::DescribeDefaultLevel(document, event, exit->Fields[3], defaultLevel) && defaultLevel.contains("secret"_s), "default secret level");
					event.Params[2] = 1;
					expect(!EventPreviews::DescribeDefaultLevel(document, event, exit->Fields[3], defaultLevel), "next level of a text");
					expect(!EventPreviews::DescribeDefaultLevel(document, event, exit->Fields[0], defaultLevel), "default level of another field");
				}
			}

			// Ambient sounds have one, unknown sounds and other events don't
			event = EventCatalog::MakeDefault(EventType::AreaAmbientSound);
			expect(EventPreviews::HasSound(event), "ambient sound");
			event.Params[0] = 7;
			expect(!EventPreviews::HasSound(event) && !EventPreviews::HasSound(EventCatalog::MakeDefault(EventType::AreaHForce)), "no sound");

			// The falloff of a light as the shader draws it
			expect(EventPreviews::GetLightStrength(10.0f, 20.0f, 60.0f) == 1.0f && EventPreviews::GetLightStrength(40.0f, 20.0f, 60.0f) == 0.125f &&
				EventPreviews::GetLightStrength(70.0f, 20.0f, 60.0f) == 0.0f, "light falloff");

			// A pulsing light starting a quarter of a pulse in is at its largest, one of no speed doesn't pulse
			{
				event = EventCatalog::MakeDefault(EventType::LightPulse);
				event.Params[8] = 6;
				event.Params[9] = 1;
				EventLightShape light;
				expect(EventPreviews::GetLight(event, light) && light.Pulse > 0.0f &&
					std::abs(EventPreviews::GetLightPulse(light, 0.0f) - light.Pulse) < 0.01f, "light pulse");
				event.Params[8] = 0;
				expect(EventPreviews::GetLight(event, light) && EventPreviews::GetLightPulse(light, 1.0f) == 0.0f, "light without pulse");
			}

			if (problems == 0) {
				LOGI("Catalog params test passed");
			}
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
				document.ExtraTileSets.push_back(Death::move(tileSet));
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

		// The last tiles of a tile set are taken by the animated tiles in a level with as many tile IDs as the tile set
		// has tiles, as in the original game (arace2.j2l), such a tile makes the space grow in the step that places it
		std::int32_t TestTileIdSpaceGrowth()
		{
			std::int32_t problems = 0;
			auto expect = [&problems](bool condition, const char* what) {
				if (!condition) {
					LOGE("Tile ID space: {}", what);
					problems++;
				}
			};

			const TileRef lastTile{ 19, TileRef::DefaultTileSet, 0 };
			const TileRef animatedTile{ 0, TileRef::Animated, 0 };

			LevelDocument document;
			document.CreateNew("test"_s, 20, Vector2i(8, 8));
			document.Animations.emplace_back().Frames.push_back(TileRef{ 2, TileRef::DefaultTileSet, 0 });
			document.TileIdSpace = 20;
			document.Layers[0].Tiles[1 + 1 * 8] = animatedTile;
			expect(document.GetAnimatedTilesOffset() == 19 && !document.IsTileAddressable(lastTile), "the last tile can't be addressed");

			CommandHistory history(document, SIZE_MAX);
			DocumentChanges changes;

			// A tile within the space doesn't change it
			auto paint = std::make_unique<SetTilesCommand>(document, 0, "Paint"_s);
			paint->Set(2, 2, TileRef{ 5, TileRef::DefaultTileSet, 0 });
			ExecuteGrowingTileIdSpace(history, Death::move(paint), changes);
			expect(document.TileIdSpace == 20 && history.GetDoneCount() == 1, "a tile within the space keeps it");

			// The last tile makes it grow, in the same step
			changes.Clear();
			paint = std::make_unique<SetTilesCommand>(document, 0, "Paint"_s);
			paint->Set(3, 2, lastTile);
			ExecuteGrowingTileIdSpace(history, Death::move(paint), changes);
			expect(history.GetDoneCount() == 2 && history.GetUndoName() == "Paint"_s, "one step named as the change");
			expect(document.GetAnimatedTilesOffset() == 20 && document.Validate(), "the space grew");
			expect(changes.HasAny(DocumentChangeKind::AnimationStructure) && changes.HasAny(DocumentChangeKind::Tiles), "the animated tiles moved");

			// Saved and loaded, the tile is still the tile and the animated tile still the animated one
			MemoryStream saved;
			expect(document.Save(saved), "saved");
			LevelDocument reloaded;
			saved.Seek(0, SeekOrigin::Begin);
			if (reloaded.Load(saved, nullptr) && !reloaded.Layers.empty()) {
				expect(reloaded.Layers[0].GetTile(3, 2) == lastTile, "the tile kept");
				expect(reloaded.Layers[0].GetTile(1, 1) == animatedTile, "the animated tile kept");
			} else {
				expect(false, "loaded");
			}

			// Undone and redone along with the tile
			history.Undo(changes);
			expect(document.TileIdSpace == 20 && document.Layers[0].GetTile(3, 2).IsEmpty(), "undone at once");
			history.Redo(changes);
			expect(document.TileIdSpace == 21 && document.Layers[0].GetTile(3, 2) == lastTile, "redone at once");
			history.Undo(changes);

			// A frame of an animated tile, in a transaction the growth is a part of it
			history.BeginTransaction("Frames"_s);
			DocumentAnimation animation = document.Animations[0];
			animation.Frames.push_back(lastTile);
			ExecuteGrowingTileIdSpace(history, std::make_unique<SetAnimationCommand>(document, 0, Death::move(animation), "Frames"_s), changes);
			history.EndTransaction();
			expect(document.TileIdSpace == 21 && history.GetDoneCount() == 2 && document.Validate(), "a frame past the space grows it");
			history.Undo(changes);
			expect(document.TileIdSpace == 20 && document.Animations[0].Frames.size() == 1, "the frame undone with the growth");

			// All tiles of the tile sets fit then, it doesn't grow again with the next one
			Formats::ExtraTileSetRecord tileSet = {};
			tileSet.Name = "extra"_s;
			tileSet.Count = 5;
			document.ExtraTileSets.push_back(Death::move(tileSet));
			paint = std::make_unique<SetTilesCommand>(document, 0, "Paint"_s);
			paint->Set(4, 2, lastTile);
			ExecuteGrowingTileIdSpace(history, Death::move(paint), changes);
			expect(document.GetAnimatedTilesOffset() == 25 && document.IsTileAddressable(TileRef{ 4, 1, 0 }), "the space fits the tile sets");

			// An override of the last tile, kept by its tile ID, makes it grow too, so it stays the tile's as the tile sets change
			{
				LevelDocument overridden;
				overridden.CreateNew("test"_s, 20, Vector2i(8, 8));
				overridden.Animations.emplace_back().Frames.push_back(TileRef{ 2, TileRef::DefaultTileSet, 0 });
				overridden.TileIdSpace = 20;
				CommandHistory overrideHistory(overridden, SIZE_MAX);
				std::uint8_t indices[Formats::LevelFile::TileSize * Formats::LevelFile::TileSize];
				std::memset(indices, 7, sizeof(indices));

				// An ID past the tile sets isn't of any tile, it's left as it is
				ExecuteGrowingTileIdSpace(overrideHistory, std::make_unique<SetTileOverrideCommand>(overridden, std::uint16_t(30), indices, nullptr, "Pixels"_s), changes);
				expect(overridden.TileIdSpace == 20, "an override past the tile sets keeps the space");
				overrideHistory.Undo(changes);

				changes.Clear();
				ExecuteGrowingTileIdSpace(overrideHistory, std::make_unique<SetTileOverrideCommand>(overridden, std::uint16_t(19), indices, nullptr, "Pixels"_s), changes);
				expect(overridden.GetAnimatedTilesOffset() == 20 && changes.HasAny(DocumentChangeKind::AnimationStructure), "an override of the last tile grows the space");
				LevelDocument::OverrideRefs refs = overridden.CaptureOverrideRefs();
				expect(refs.Diffuse.size() == 1 && refs.Diffuse[0] == lastTile, "the override is the tile's");
				overrideHistory.Undo(changes);
				expect(overridden.TileIdSpace == 20 && overridden.DiffuseOverrides.empty(), "the override undone with the growth");
			}

			if (problems == 0) {
				LOGI("Tile ID space test passed");
			}
			return problems;
		}

		// Races with laps, Capture the Flag and multiplayer spawn points are saved as the events decide them
		std::int32_t TestEventFlags()
		{
			std::int32_t problems = 0;
			auto expect = [&problems](bool condition, const char* what) {
				if (!condition) {
					LOGE("Event flags: {}", what);
					problems++;
				}
			};

			auto savedFlags = [](const LevelDocument& document, bool exportOnly) {
				MemoryStream saved;
				document.Save(saved, exportOnly);
				saved.Seek(0, SeekOrigin::Begin);
				Formats::LevelHeader header;
				return (Formats::LevelFile::ReadHeader(saved, header) ? header.Flags : LevelFlags::NotEditable);
			};
			auto makeEvent = [](EventType type, std::uint8_t param2 = 0, std::uint8_t flags = 0) {
				Formats::EventRecord event = LevelDocument::GetEmptyEvent();
				event.Type = std::uint16_t(type);
				event.Flags = flags;
				event.Params[2] = param2;
				return event;
			};
			constexpr LevelFlags EventFlags = LevelDocument::EventFlags;

			LevelDocument document;
			document.CreateNew("test"_s, 20, Vector2i(16, 16));
			// Set by hand, as a level saved before could have them, but without any event that makes them
			document.Flags = LevelFlags::HasLaps | LevelFlags::HasCaptureTheFlag | LevelFlags::HasMultiplayerSpawnPoints | LevelFlags::HasPit;
			expect((savedFlags(document, false) & (EventFlags | LevelFlags::HasPit)) == LevelFlags::HasPit, "flags without their events are dropped, the others kept");

			// Bases and a finish line count only in a level for multiplayer, events of a generator never
			document.Flags = LevelFlags::None;
			document.SetEvent(1, 1, makeEvent(EventType::CtfBase));
			document.SetEvent(2, 1, makeEvent(EventType::WarpOrigin, 1));
			document.SetEvent(3, 1, makeEvent(EventType::LevelStartMultiplayer, 0, Formats::EventRecord::Generator));
			expect((savedFlags(document, false) & EventFlags) == LevelFlags::None, "only in a level for multiplayer");
			document.Flags = LevelFlags::IsMultiplayerLevel;
			expect((savedFlags(document, true) & EventFlags) == (LevelFlags::HasLaps | LevelFlags::HasCaptureTheFlag), "a base and a finish line make a CTF race level");

			// A multiplayer start, off the grid too, in any level
			document.Flags = LevelFlags::None;
			Formats::OffGridEventRecord start = {};
			start.X = 40;
			start.Y = 40;
			start.Event = makeEvent(EventType::LevelStartMultiplayer);
			document.OffGridEvents.push_back(start);
			expect((savedFlags(document, false) & EventFlags) == LevelFlags::HasMultiplayerSpawnPoints, "a multiplayer start makes spawn points");

			// A warp without "Set Lap" isn't a finish line, a coin warp with it is
			document.Flags = LevelFlags::IsMultiplayerLevel;
			document.SetEvent(1, 1, LevelDocument::GetEmptyEvent());
			document.SetEvent(2, 1, makeEvent(EventType::WarpOrigin, 0));
			expect((savedFlags(document, false) & EventFlags) == LevelFlags::HasMultiplayerSpawnPoints, "no base and no finish line");
			document.SetEvent(4, 1, makeEvent(EventType::WarpCoinBonus, 1));
			expect((savedFlags(document, false) & EventFlags) == (LevelFlags::HasLaps | LevelFlags::HasMultiplayerSpawnPoints), "a coin warp with Set Lap is a finish line");

			// Loaded as the file has them
			MemoryStream saved;
			document.Save(saved);
			saved.Seek(0, SeekOrigin::Begin);
			LevelDocument reloaded;
			expect(reloaded.Load(saved, nullptr) && (reloaded.Flags & EventFlags) == (LevelFlags::HasLaps | LevelFlags::HasMultiplayerSpawnPoints), "loaded with the flags");

			if (problems == 0) {
				LOGI("Event flags test passed");
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

			// Error diffusion keeps the pixels opaque too
			options.Dither = false;
			options.ErrorDiffusion = true;
			Formats::TileSetData diffused;
			expect(SliceTileSet(image, options, diffused, result), "sliced with error diffusion");
			bool diffusedOpaque = true;
			for (std::int32_t p = 0; p < 32 * 32; p++) {
				diffusedOpaque &= (diffused.Images[1 * Formats::TileSetData::ImageBytesPerTile + p] != 0);
			}
			expect(diffusedOpaque, "error diffusion keeps opaque pixels");

			// A sheet with a margin of 2 and gutters of 3 pixels, magenta for transparent: two tiles, the first one empty
			PngImage sheet;
			sheet.Create(2 + 32 + 3 + 32, 2 + 32, false);
			for (std::int32_t y = 0; y < sheet.Height; y++) {
				for (std::int32_t x = 0; x < sheet.Width; x++) {
					std::uint8_t* pixel = &sheet.Pixels[(std::size_t(y) * std::size_t(sheet.Width) + std::size_t(x)) * 4];
					bool second = (x >= 2 + 32 + 3);
					pixel[0] = (second ? 10 : 255);
					pixel[1] = (second ? 20 : 0);
					pixel[2] = (second ? 30 : 255);
					pixel[3] = 255;
				}
			}
			TileImportOptions sheetOptions;
			sheetOptions.Colors = TileImportColors::TrueColor;
			sheetOptions.OffsetX = 2;
			sheetOptions.OffsetY = 2;
			sheetOptions.SpacingX = 3;
			sheetOptions.UseColorKey = true;
			Vector2i grid = GetTileGrid(sheet, sheetOptions);
			Formats::TileSetData sliced;
			expect(grid.X == 2 && grid.Y == 1, "grid with margin and gutters");
			expect(SliceTileSet(sheet, sheetOptions, sliced, result) && !result.AddedEmptyTile && result.TileCount == 2, "magenta is transparent");
			expect(sliced.TileCount == 2 && sliced.Images[1 * Formats::TileSetData::ImageBytesPerTile] == 10, "tiles past the gutter");

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
				read.HasPlaySettings && read.PlayCharacter == 3 && read.PlayDifficulty == 1 && read.PlayGameMode == 4 && read.PlayPlayerCount == 3;

			// Export leaves it out
			MemoryStream exported;
			LevelDocument exportedLoaded;
			ok = ok && document.Save(exported, true, 1);
			exported.Seek(0, SeekOrigin::Begin);
			ok = ok && exportedLoaded.Load(exported, nullptr) && !DocumentViewState::Read(exportedLoaded, read);

			// Where older builds kept a play-start marker, 0 is written and its flag 0x40 is never set, so they see none and
			// what's added later stays where it would be, and the marker of an older chunk is skipped
			constexpr std::size_t MarkerOffset = 25;
			constexpr std::size_t ChunkSize = MarkerOffset + 8;
			const Formats::ChunkRecord* chunk = document.FindChunk(DocumentViewState::Chunk);
			ok = ok && chunk != nullptr && chunk->Data.size() == ChunkSize && (chunk->Data[1] & 0x40) == 0;
			for (std::size_t i = MarkerOffset; ok && i < ChunkSize; i++) {
				ok = (chunk->Data[i] == 0);
			}
			if (ok) {
				SmallVector<std::uint8_t, 0> older;
				older.assign(chunk->Data.data(), chunk->Data.data() + chunk->Data.size());
				older[1] |= 0x40;
				older[MarkerOffset] = 0x10;
				older[MarkerOffset + 4] = 0x20;
				document.SetChunk(DocumentViewState::Chunk, older);
				DocumentViewState fromOlder;
				ok = DocumentViewState::Read(document, fromOlder) && fromOlder.HasCamera && fromOlder.CameraCenter == state.CameraCenter &&
					fromOlder.HasPlaySettings && fromOlder.PlayCharacter == 3 && fromOlder.PlayPlayerCount == 3;
			}

			if (!ok) {
				LOGE("View state: the edoc chunk isn't the same after it was saved and loaded, export kept it, or its layout changed");
				return 1;
			}
			return 0;
		}

		// Where a layer is placed for a view, against JJ2+. Each case is a measurement of JJ2+ v6.6h, taken with
		// Tests/Level/LayerProbe.j2as in a level of 256x64 tiles: the layer coordinate at the left and the top edge of
		// the view (jjLAYER::getXPosition() and getYPosition()), the camera at the top left of the view. The models are
		// what the converter makes of JJ2+'s - NORMAL with an auto speed loses its speed, BOTHSPEEDS is the default one -
		// and the offsets of a layer with an auto speed are how far it had moved the layer by then (see TileMap::OnUpdate())
		std::int32_t TestLayerPlacement()
		{
			using M = Tiles::LayerSpeedModel;
			struct Case {
				std::int32_t ViewWidth, ViewHeight;
				float CameraX, CameraY;
				M Model;
				float Speed, AutoSpeed, OffsetX, OffsetY;
				bool Repeat, InherentOffset;
				std::int32_t Width, Height;
				float X, Y;
			};
			static const Case Cases[] = {
				{ 800, 450, 1000.0f, 517.0f, M::Default, 0.5f, 0.0f, 0.0f, 0.0f, false, false, 40, 30, 380.0f, 196.0f },
				{ 800, 450, 2500.0f, 1000.0f, M::Default, 0.5f, 0.0f, 0.0f, 0.0f, false, true, 40, 30, 1130.0f, 312.5f },
				{ 800, 450, 1000.0f, 517.0f, M::Default, 0.5f, 0.0f, 0.0f, 0.0f, true, true, 40, 30, 380.0f, 196.0f },
				{ 800, 450, 0.0f, 0.0f, M::Default, 1.5f, 0.0f, 0.0f, 0.0f, false, false, 40, 30, 120.0f, 62.5f },
				{ 800, 450, 1000.0f, 517.0f, M::Default, 0.0f, 0.0f, 118.0f, 118.0f, false, false, 40, 30, -122.0f, -7.0f },
				{ 800, 450, 2500.0f, 1000.0f, M::Default, 0.0f, 0.0f, 122.0f, 122.0f, false, true, 40, 30, -118.0f, -128.0f },
				{ 800, 450, 1000.0f, 517.0f, M::AlwaysOnTop, 0.0f, 0.0f, 0.0f, 0.0f, false, false, 40, 30, 0.0f, 0.0f },
				{ 800, 450, 2500.0f, 1000.0f, M::Default, 0.5f, 0.0f, 122.0f, 122.0f, false, false, 40, 30, 1252.0f, 559.5f },
				{ 800, 450, 1000.0f, 517.0f, M::Default, 0.5f, 0.0f, 118.0f, 118.0f, false, true, 40, 30, 498.0f, 189.0f },
				{ 800, 450, 0.0f, 0.0f, M::Default, 0.5f, 0.0f, 126.0f, 126.0f, true, true, 40, 30, 6.0f, 63.5f },
				{ 800, 450, 1000.0f, 517.0f, M::FromStart, 0.5f, 0.0f, 0.0f, 0.0f, false, false, 40, 30, 500.0f, 258.5f },
				{ 800, 450, 2500.0f, 1000.0f, M::FromStart, 0.0f, 0.0f, 0.0f, 0.0f, false, true, 40, 30, 0.0f, 0.0f },
				{ 800, 450, 0.0f, 0.0f, M::FromStart, 1.5f, 0.0f, 126.0f, 126.0f, false, true, 40, 30, 126.0f, 126.0f },
				{ 800, 450, 1000.0f, 517.0f, M::FitLevel, 0.0f, 0.0f, 0.0f, 0.0f, false, false, 40, 30, 64.935f, 165.0f },
				{ 800, 450, 2500.0f, 1000.0f, M::FitLevel, 0.5f, 0.0f, 0.0f, 0.0f, false, true, 12, 8, -140.693f, -121.402f },
				{ 800, 450, 0.0f, 0.0f, M::FitLevel, 0.5f, 0.0f, 0.0f, 0.0f, false, true, 28, 14, 0.0f, 0.0f },
				{ 800, 450, 1000.0f, 517.0f, M::FitLevel, 0.5f, 0.0f, 0.0f, 0.0f, false, true, 100, 50, 324.675f, 372.059f },
				{ 800, 450, 0.0f, 0.0f, M::FitLevel, 0.0f, 0.0f, 0.0f, 0.0f, false, true, 40, 30, 0.0f, 0.0f },
				{ 800, 450, 1000.0f, 517.0f, M::SpeedMultipliers, 0.5f, 0.0f, 0.0f, 0.0f, false, false, 40, 30, -345.887f, -152.206f },
				{ 800, 450, 2500.0f, 1000.0f, M::SpeedMultipliers, 1.5f, 0.5f, 0.0f, 0.0f, false, false, 40, 30, -929.437f, -393.398f },
				{ 800, 450, 0.0f, 0.0f, M::SpeedMultipliers, 0.0f, 0.5f, 0.0f, 0.0f, false, false, 40, 30, 0.0f, 0.0f },
				{ 800, 450, 1000.0f, 517.0f, M::Default, 1.5f, 0.0f, 37.0f, -23.0f, false, false, 40, 30, 1657.0f, 815.0f },
				{ 800, 450, 1000.0f, 517.0f, M::AlwaysOnTop, 1.0f, 0.0f, 37.0f, -23.0f, false, false, 40, 30, 0.0f, 0.0f },
				{ 800, 450, 2500.0f, 1000.0f, M::FromStart, 1.5f, 0.0f, 37.0f, -23.0f, false, false, 40, 30, 3787.0f, 1477.0f },
				{ 800, 450, 2500.0f, 1000.0f, M::FitLevel, 0.5f, 0.0f, 37.0f, -23.0f, false, false, 40, 30, 199.338f, 296.149f },
				{ 800, 450, 1000.0f, 517.0f, M::SpeedMultipliers, 1.0f, 0.0f, 37.0f, -23.0f, false, false, 40, 30, -654.775f, -327.412f },
				{ 800, 450, 0.0f, 0.0f, M::SpeedMultipliers, 0.25f, 0.75f, 0.0f, 0.0f, false, true, 12, 8, -200.0f, -112.5f },
				{ 800, 450, 2500.0f, 1000.0f, M::Default, 0.5f, 0.0f, 179.0f, 119.0f, false, true, 40, 30, 1309.0f, 431.5f },
				{ 800, 450, 2500.0f, 1000.0f, M::Default, 1.25f, 0.0f, -50.0f, 61.0f, false, true, 40, 30, 3135.0f, 1217.25f },
				{ 800, 450, 0.0f, 0.0f, M::FromStart, 1.25f, 0.0f, -50.0f, 61.0f, false, true, 40, 30, -50.0f, 61.0f },
				{ 640, 480, 1000.0f, 517.0f, M::Default, 0.5f, 0.0f, 0.0f, 0.0f, false, false, 40, 30, 420.0f, 188.5f },
				{ 640, 480, 2500.0f, 1000.0f, M::Default, 0.5f, 0.0f, 0.0f, 0.0f, false, true, 40, 30, 1170.0f, 290.0f },
				{ 640, 480, 1000.0f, 517.0f, M::Default, 0.5f, 0.0f, 0.0f, 0.0f, true, true, 40, 30, 420.0f, 188.5f },
				{ 640, 480, 7552.0f, 1568.0f, M::Default, 1.5f, 0.0f, 0.0f, 0.0f, false, false, 40, 30, 11408.0f, 2422.0f },
				{ 640, 480, 1000.0f, 517.0f, M::Default, 0.0f, 0.0f, 106.5f, 106.5f, false, false, 40, 30, -53.5f, -33.5f },
				{ 640, 480, 2500.0f, 1000.0f, M::Default, 0.0f, 0.0f, 110.5f, 110.5f, false, true, 40, 30, -49.5f, -169.5f },
				{ 640, 480, 1000.0f, 517.0f, M::AlwaysOnTop, 0.0f, 0.0f, 0.0f, 0.0f, false, false, 40, 30, 0.0f, 0.0f },
				{ 640, 480, 2500.0f, 1000.0f, M::Default, 0.5f, 0.0f, 110.5f, 110.5f, false, false, 40, 30, 1280.5f, 540.5f },
				{ 640, 480, 1000.0f, 517.0f, M::Default, 0.5f, 0.0f, 106.5f, 106.5f, false, true, 40, 30, 526.5f, 155.0f },
				{ 640, 480, 7552.0f, 1568.0f, M::Default, 0.5f, 0.0f, 114.5f, 114.5f, true, true, 40, 30, 3810.5f, 828.5f },
				{ 640, 480, 1000.0f, 517.0f, M::FromStart, 0.5f, 0.0f, 0.0f, 0.0f, false, false, 40, 30, 500.0f, 258.5f },
				{ 640, 480, 2500.0f, 1000.0f, M::FromStart, 0.0f, 0.0f, 0.0f, 0.0f, false, true, 40, 30, 0.0f, 0.0f },
				{ 640, 480, 7552.0f, 1568.0f, M::FromStart, 1.5f, 0.0f, 114.5f, 114.5f, false, true, 40, 30, 11442.5f, 2466.5f },
				{ 640, 480, 1000.0f, 517.0f, M::FitLevel, 0.0f, 0.0f, 0.0f, 0.0f, false, false, 40, 30, 84.746f, 158.265f },
				{ 640, 480, 2500.0f, 1000.0f, M::FitLevel, 0.5f, 0.0f, 0.0f, 0.0f, false, true, 12, 8, -84.746f, -142.857f },
				{ 640, 480, 7552.0f, 1568.0f, M::FitLevel, 0.5f, 0.0f, 0.0f, 0.0f, false, true, 28, 14, 256.0f, -32.0f },
				{ 640, 480, 1000.0f, 517.0f, M::FitLevel, 0.5f, 0.0f, 0.0f, 0.0f, false, true, 100, 50, 338.983f, 369.286f },
				{ 640, 480, 7552.0f, 1568.0f, M::FitLevel, 0.0f, 0.0f, 0.0f, 0.0f, false, true, 40, 30, 640.0f, 480.0f },
				{ 640, 480, 1000.0f, 517.0f, M::SpeedMultipliers, 0.5f, 0.0f, 0.0f, 0.0f, false, false, 40, 30, -277.627f, -160.867f },
				{ 640, 480, 2500.0f, 1000.0f, M::SpeedMultipliers, 1.5f, 0.5f, 0.0f, 0.0f, false, false, 40, 30, -748.136f, -413.878f },
				{ 640, 480, 7552.0f, 1568.0f, M::SpeedMultipliers, 0.0f, 0.5f, 0.0f, 0.0f, false, false, 40, 30, -320.0f, -240.0f },
				{ 640, 480, 1000.0f, 517.0f, M::Default, 1.5f, 0.0f, 37.0f, -23.0f, false, false, 40, 30, 1617.0f, 822.5f },
				{ 640, 480, 1000.0f, 517.0f, M::AlwaysOnTop, 1.0f, 0.0f, 37.0f, -23.0f, false, false, 40, 30, 0.0f, 0.0f },
				{ 640, 480, 2500.0f, 1000.0f, M::FromStart, 1.5f, 0.0f, 37.0f, -23.0f, false, false, 40, 30, 3787.0f, 1477.0f },
				{ 640, 480, 2500.0f, 1000.0f, M::FitLevel, 0.5f, 0.0f, 37.0f, -23.0f, false, false, 40, 30, 248.864f, 283.122f },
				{ 640, 480, 1000.0f, 517.0f, M::SpeedMultipliers, 1.0f, 0.0f, 37.0f, -23.0f, false, false, 40, 30, -518.254f, -344.735f },
				{ 640, 480, 7552.0f, 1568.0f, M::SpeedMultipliers, 0.25f, 0.75f, 0.0f, 0.0f, false, true, 12, 8, -480.0f, -360.0f },
				{ 640, 480, 2500.0f, 1000.0f, M::Default, 0.5f, 0.0f, -60.875f, -120.875f, false, true, 40, 30, 1109.125f, 169.125f },
				{ 640, 480, 2500.0f, 1000.0f, M::Default, 1.25f, 0.0f, -50.0f, 61.0f, false, true, 40, 30, 3115.0f, 1206.0f },
				{ 640, 480, 7552.0f, 1568.0f, M::FromStart, 1.25f, 0.0f, -50.0f, 61.0f, false, true, 40, 30, 9390.0f, 2021.0f },
				{ 320, 200, 1000.0f, 517.0f, M::Default, 0.5f, 0.0f, 0.0f, 0.0f, false, false, 40, 30, 500.0f, 258.5f },
				{ 320, 200, 2500.0f, 1000.0f, M::Default, 0.5f, 0.0f, 0.0f, 0.0f, false, true, 40, 30, 1250.0f, 500.0f },
				{ 320, 200, 1000.0f, 517.0f, M::Default, 0.5f, 0.0f, 0.0f, 0.0f, true, true, 40, 30, 500.0f, 258.5f },
				{ 320, 200, 7872.0f, 1848.0f, M::Default, 1.5f, 0.0f, 0.0f, 0.0f, false, false, 40, 30, 11808.0f, 2772.0f },
				{ 320, 200, 1000.0f, 517.0f, M::Default, 0.0f, 0.0f, 110.0f, 110.0f, false, false, 40, 30, 110.0f, 110.0f },
				{ 320, 200, 2500.0f, 1000.0f, M::Default, 0.0f, 0.0f, 114.0f, 114.0f, false, true, 40, 30, 114.0f, 114.0f },
				{ 320, 200, 1000.0f, 517.0f, M::AlwaysOnTop, 0.0f, 0.0f, 0.0f, 0.0f, false, false, 40, 30, 0.0f, 0.0f },
				{ 320, 200, 2500.0f, 1000.0f, M::Default, 0.5f, 0.0f, 114.0f, 114.0f, false, false, 40, 30, 1364.0f, 614.0f },
				{ 320, 200, 1000.0f, 517.0f, M::Default, 0.5f, 0.0f, 110.0f, 110.0f, false, true, 40, 30, 610.0f, 368.5f },
				{ 320, 200, 7872.0f, 1848.0f, M::Default, 0.5f, 0.0f, 118.0f, 118.0f, true, true, 40, 30, 4054.0f, 1042.0f },
				{ 320, 200, 1000.0f, 517.0f, M::FromStart, 0.5f, 0.0f, 0.0f, 0.0f, false, false, 40, 30, 500.0f, 258.5f },
				{ 320, 200, 2500.0f, 1000.0f, M::FromStart, 0.0f, 0.0f, 0.0f, 0.0f, false, true, 40, 30, 0.0f, 0.0f },
				{ 320, 200, 7872.0f, 1848.0f, M::FromStart, 1.5f, 0.0f, 118.0f, 118.0f, false, true, 40, 30, 11926.0f, 2890.0f },
				{ 320, 200, 1000.0f, 517.0f, M::FitLevel, 0.0f, 0.0f, 0.0f, 0.0f, false, false, 40, 30, 121.951f, 212.619f },
				{ 320, 200, 2500.0f, 1000.0f, M::FitLevel, 0.5f, 0.0f, 0.0f, 0.0f, false, true, 12, 8, 20.325f, 30.303f },
				{ 320, 200, 7872.0f, 1848.0f, M::FitLevel, 0.5f, 0.0f, 0.0f, 0.0f, false, true, 28, 14, 576.0f, 248.0f },
				{ 320, 200, 1000.0f, 517.0f, M::FitLevel, 0.5f, 0.0f, 0.0f, 0.0f, false, true, 100, 50, 365.854f, 391.667f },
				{ 320, 200, 7872.0f, 1848.0f, M::FitLevel, 0.0f, 0.0f, 0.0f, 0.0f, false, true, 40, 30, 960.0f, 760.0f },
				{ 320, 200, 1000.0f, 517.0f, M::SpeedMultipliers, 0.5f, 0.0f, 0.0f, 0.0f, false, false, 40, 30, -139.675f, -72.024f },
				{ 320, 200, 2500.0f, 1000.0f, M::SpeedMultipliers, 1.5f, 0.5f, 0.0f, 0.0f, false, false, 40, 30, -378.374f, -191.775f },
				{ 320, 200, 7872.0f, 1848.0f, M::SpeedMultipliers, 0.0f, 0.5f, 0.0f, 0.0f, false, false, 40, 30, -160.0f, -100.0f },
				{ 320, 200, 1000.0f, 517.0f, M::Default, 1.5f, 0.0f, 37.0f, -23.0f, false, false, 40, 30, 1537.0f, 752.5f },
				{ 320, 200, 1000.0f, 517.0f, M::AlwaysOnTop, 1.0f, 0.0f, 37.0f, -23.0f, false, false, 40, 30, 0.0f, 0.0f },
				{ 320, 200, 2500.0f, 1000.0f, M::FromStart, 1.5f, 0.0f, 37.0f, -23.0f, false, false, 40, 30, 3787.0f, 1477.0f },
				{ 320, 200, 2500.0f, 1000.0f, M::FitLevel, 0.5f, 0.0f, 37.0f, -23.0f, false, false, 40, 30, 341.878f, 388.255f },
				{ 320, 200, 1000.0f, 517.0f, M::SpeedMultipliers, 1.0f, 0.0f, 37.0f, -23.0f, false, false, 40, 30, -242.35f, -167.048f },
				{ 320, 200, 7872.0f, 1848.0f, M::SpeedMultipliers, 0.25f, 0.75f, 0.0f, 0.0f, false, true, 12, 8, -240.0f, -150.0f },
				{ 320, 200, 2500.0f, 1000.0f, M::Default, 0.5f, 0.0f, -63.5f, -123.5f, false, true, 40, 30, 1186.5f, 376.5f },
				{ 320, 200, 2500.0f, 1000.0f, M::Default, 1.25f, 0.0f, -50.0f, 61.0f, false, true, 40, 30, 3075.0f, 1311.0f },
				{ 320, 200, 7872.0f, 1848.0f, M::FromStart, 1.25f, 0.0f, -50.0f, 61.0f, false, true, 40, 30, 9790.0f, 2371.0f },
			};

			std::int32_t problems = 0;
			std::int32_t index = 0;
			for (const Case& c : Cases) {
				Tiles::LayerDescription layer = {};
				layer.SpeedModelX = c.Model;
				layer.SpeedModelY = c.Model;
				layer.SpeedX = c.Speed;
				layer.SpeedY = c.Speed;
				layer.AutoSpeedX = c.AutoSpeed;
				layer.AutoSpeedY = c.AutoSpeed;
				layer.OffsetX = c.OffsetX;
				layer.OffsetY = c.OffsetY;
				layer.RepeatX = c.Repeat;
				layer.RepeatY = c.Repeat;
				layer.UseInherentOffset = c.InherentOffset;

				Rectf view(c.CameraX, c.CameraY, float(c.ViewWidth), float(c.ViewHeight));
				Vector2f origin = layer.GetGameplayOrigin(Vector2i(c.Width, c.Height), Vector2i(256, 64), view);
				float x = view.X - origin.X;
				float y = view.Y - origin.Y;
				// A layer that moves with the level is drawn a pixel higher than JJ2+ places it, as the sprite layer is
				float expectedY = (c.Model != M::AlwaysOnTop ? c.Y + 1.0f : c.Y);
				if (std::abs(x - c.X) > 0.01f || std::abs(y - expectedY) > 0.01f) {
					LOGE("Layer placement: case {} (model {} in {}x{} at {}, {}) is at {}, {} instead of {}, {}", index, std::int32_t(c.Model),
						c.ViewWidth, c.ViewHeight, c.CameraX, c.CameraY, x, y, c.X, expectedY);
					problems++;
				}
				index++;
			}
			return problems;
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
				// Not the working directory, the game may be installed where it can't write
				String path = fs::CombinePath(fs::GetTempDirectory(), String("selftest-palette."_s + extension));
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
				String path = fs::CombinePath(fs::GetTempDirectory(), "selftest-palette-grid.png"_s);
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

		// Old changes are forgotten when the history takes more memory than allowed, the last one is always kept
		std::int32_t TestHistoryBudget()
		{
			std::int32_t problems = 0;
			auto expect = [&problems](bool condition, const char* what) {
				if (!condition) {
					LOGE("History budget: {}", what);
					problems++;
				}
			};

			LevelDocument document;
			document.CreateNew("test"_s, 100, Vector2i(64, 64));
			constexpr std::size_t Budget = 16 * 1024;
			CommandHistory history(document, Budget);
			DocumentChanges changes;
			TestRandom random(91);
			constexpr std::int32_t StepCount = 200;
			for (std::int32_t i = 0; i < StepCount; i++) {
				auto command = std::make_unique<SetTilesCommand>(document, 0, "Tiles"_s);
				for (std::int32_t j = 0; j < 50; j++) {
					command->Set(random.Next(64), random.Next(64), TileRef{ std::uint16_t(1 + random.Next(99)), TileRef::DefaultTileSet, 0 });
				}
				history.Execute(Death::move(command), changes);
			}
			expect(history.GetMemoryUsage() <= Budget, "memory within the budget");
			expect(history.GetDoneCount() > 0 && history.GetDoneCount() < StepCount, "old changes forgotten");

			MemoryStream changed;
			Export(document, changed);
			std::int32_t doneCount = history.GetDoneCount();
			while (history.Undo(changes)) {}
			// The saved state was forgotten with the oldest changes, it can't be reached anymore
			expect(history.IsDirty(), "dirty after the reachable changes were undone");
			for (std::int32_t i = 0; i < doneCount; i++) {
				history.Redo(changes);
			}
			MemoryStream redone;
			Export(document, redone);
			expect(AreEqual(changed, redone), "the same after redo");

			// A change larger than the whole budget is kept by itself
			auto large = std::make_unique<SetTilesCommand>(document, 0, "Large"_s);
			for (std::int32_t y = 0; y < 64; y++) {
				for (std::int32_t x = 0; x < 64; x++) {
					large->Set(x, y, TileRef{ std::uint16_t(1 + random.Next(99)), TileRef::DefaultTileSet, std::uint8_t(random.Next(4)) });
				}
			}
			history.Execute(Death::move(large), changes);
			expect(history.GetDoneCount() >= 1 && history.GetUndoName() == "Large"_s, "a large change is kept");

			if (problems == 0) {
				LOGI("History budget test passed");
			}
			return problems;
		}

		// Changes are merged only while the same value is dragged
		std::int32_t TestMergePolicy()
		{
			std::int32_t problems = 0;
			auto expect = [&problems](bool condition, const char* what) {
				if (!condition) {
					LOGE("Merge policy: {}", what);
					problems++;
				}
			};

			LevelDocument document;
			document.CreateNew("test"_s, 100, Vector2i(16, 16));
			CommandHistory history(document, SIZE_MAX);
			DocumentChanges changes;
			auto setWater = [&](std::uint16_t level, bool final) {
				Formats::LevelProperties properties = document.Properties;
				properties.WaterLevel = level;
				history.Execute(std::make_unique<SetLevelPropertiesCommand>(document, document.Flags, properties, "Level properties"_s), changes, true);
				if (final) {
					history.BreakMerge();
				}
			};
			auto setMusic = [&](StringView music) {
				Formats::LevelProperties properties = document.Properties;
				properties.Music = music;
				history.Execute(std::make_unique<SetLevelPropertiesCommand>(document, document.Flags, properties, "Level properties"_s), changes, true);
			};

			setWater(10, false);
			setWater(20, false);
			setWater(30, true);
			expect(history.GetDoneCount() == 1 && document.Properties.WaterLevel == 30, "a dragged value is one step");
			setWater(40, true);
			expect(history.GetDoneCount() == 2, "the next drag is a step of its own");
			setWater(50, false);
			setMusic("a.it"_s);
			expect(history.GetDoneCount() == 4, "another property is a step of its own");
			history.Undo(changes);
			setWater(60, false);
			expect(history.GetDoneCount() == 4, "nothing is merged after undo");

			// A rename and a change of the speed of the same layer
			Formats::LayerRecord record = document.Layers[0].Record;
			history.Execute(std::make_unique<SetLayerPropertiesCommand>(document, 0, record, "Renamed"_s, "Layer properties"_s), changes, true);
			record.SpeedX = 2.0f;
			history.Execute(std::make_unique<SetLayerPropertiesCommand>(document, 0, record, "Renamed"_s, "Layer properties"_s), changes, true);
			expect(history.GetDoneCount() == 6, "a rename and a speed are two steps");

			// The same property of two different events off the grid
			for (std::int32_t i = 0; i < 2; i++) {
				Formats::OffGridEventRecord& event = document.OffGridEvents.emplace_back();
				event.X = std::uint32_t(10 + i * 40);
				event.Y = 20;
				event.Event = LevelDocument::GetEmptyEvent();
				event.Event.Type = 1;
			}
			for (std::int32_t i = 0; i < 2; i++) {
				SmallVector<Formats::OffGridEventRecord, 0> events;
				for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
					events.push_back(event);
				}
				events[i].Event.Params[0] = 5;
				history.Execute(std::make_unique<SetOffGridEventsCommand>(document, Death::move(events), "Speed"_s), changes, true);
			}
			expect(history.GetDoneCount() == 8, "two events off the grid are two steps");

			// A value dragged over more events off the grid at once is one step, over other events another one
			{
				Formats::OffGridEventRecord& third = document.OffGridEvents.emplace_back();
				third.X = 90;
				third.Y = 20;
				third.Event = LevelDocument::GetEmptyEvent();
				third.Event.Type = 1;
			}
			auto changeOffGrid = [&](std::initializer_list<std::uint32_t> indices, std::uint8_t value) {
				SmallVector<Formats::OffGridEventRecord, 0> events;
				for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
					events.push_back(event);
				}
				for (std::uint32_t index : indices) {
					events[index].Event.Params[1] = value;
				}
				history.Execute(std::make_unique<SetOffGridEventsCommand>(document, Death::move(events), "Strength"_s), changes, true);
			};
			history.BreakMerge();
			changeOffGrid({ 0, 2 }, 1);
			changeOffGrid({ 0, 2 }, 2);
			changeOffGrid({ 0, 2 }, 3);
			expect(history.GetDoneCount() == 9, "a value of more events off the grid dragged is one step");
			changeOffGrid({ 0, 1 }, 4);
			expect(history.GetDoneCount() == 10, "other events off the grid are a step of their own");

			// Events of the grid and off it changed together, as a transaction on each step of a drag
			auto changeBoth = [&](std::uint8_t value) {
				history.BeginTransaction("Change Events"_s);
				auto command = std::make_unique<SetEventsCommand>(document, "Strength"_s);
				Formats::EventRecord event = LevelDocument::GetEmptyEvent();
				event.Type = 1;
				event.Params[1] = value;
				command->Set(2, 3, event);
				command->Set(4, 5, event);
				history.Execute(Death::move(command), changes, true);
				SmallVector<Formats::OffGridEventRecord, 0> events;
				for (const Formats::OffGridEventRecord& offGrid : document.OffGridEvents) {
					events.push_back(offGrid);
				}
				events[1].Event.Params[2] = value;
				history.Execute(std::make_unique<SetOffGridEventsCommand>(document, Death::move(events), "Strength"_s), changes, true);
				history.EndTransaction();
			};
			history.BreakMerge();
			changeBoth(10);
			changeBoth(11);
			changeBoth(12);
			expect(history.GetDoneCount() == 11 && document.GetEvent(4, 5).Params[1] == 12 && document.OffGridEvents[1].Event.Params[2] == 12,
				"a value dragged over events of the grid and off it is one step");
			// The end of the drag, the next one is a step of its own
			history.BreakMerge();
			changeBoth(13);
			expect(history.GetDoneCount() == 12, "the next drag is a step of its own");
			history.Undo(changes);
			history.Undo(changes);
			expect(document.GetEvent(4, 5).Type == 0 && document.OffGridEvents[1].Event.Params[2] == 0, "the merged step undone at once");

			if (problems == 0) {
				LOGI("Merge policy test passed");
			}
			return problems;
		}

		// What isn't a change of the level stays as it is when a snapshot is restored, notes are undone as changes
		std::int32_t TestOutsideHistoryState()
		{
			std::int32_t problems = 0;
			auto expect = [&problems](bool condition, const char* what) {
				if (!condition) {
					LOGE("State outside of the history: {}", what);
					problems++;
				}
			};

			LevelDocument document;
			document.CreateNew("test"_s, 100, Vector2i(16, 16));
			CommandHistory history(document, SIZE_MAX);
			DocumentChanges changes;
			constexpr std::uint32_t TestChunk = Formats::LevelFile::MakeChunkTag('e', 't', 's', 't');
			static const std::uint8_t TestChunkData[] = { 1, 2, 3 };

			auto snapshot = SnapshotCommand::Begin(document, SnapshotParts::Layers | SnapshotParts::Chunks, "Change"_s);
			document.SetChunk(TestChunk, TestChunkData);
			document.Layers[0].Tiles[0] = TileRef{ 5, TileRef::DefaultTileSet, 0 };
			snapshot->End(document, changes);
			history.Record(Death::move(snapshot));

			SmallVector<Annotation, 0> annotations;
			Annotation& note = annotations.emplace_back();
			note.X = 5;
			note.Y = 6;
			note.Text = "Note"_s;
			SmallVector<std::uint8_t, 0> data;
			Annotation::Serialize(annotations, data);
			history.Execute(std::make_unique<SetChunkCommand>(document, Annotation::Chunk, data, "Add note"_s), changes);

			// Not changes of the level, they're written outside of the history
			DocumentViewState view;
			view.HasCamera = true;
			view.CameraCenter = Vector2f(1.0f, 2.0f);
			view.CameraZoom = 1.0f;
			view.Write(document);
			document.Layers[1].EditorFlags = DocumentLayer::EditorHidden;

			history.Undo(changes);
			Annotation::Read(document, annotations);
			expect(annotations.empty(), "the note is undone");
			history.Undo(changes);
			expect(document.FindChunk(TestChunk) == nullptr && document.Layers[0].Tiles[0].IsEmpty(), "the snapshot is restored");
			expect(document.FindChunk(DocumentViewState::Chunk) != nullptr, "the view is kept");
			expect(document.Layers[1].EditorFlags == DocumentLayer::EditorHidden, "the hidden layer is kept");
			history.Redo(changes);
			history.Redo(changes);
			Annotation::Read(document, annotations);
			expect(annotations.size() == 1 && annotations[0].Text == "Note"_s && document.FindChunk(TestChunk) != nullptr, "both are redone");

			std::uint32_t outsideChanges = history.GetOutsideChangeCount();
			history.MarkDirty();
			expect(history.GetOutsideChangeCount() == outsideChanges + 1 && history.IsDirty(), "changes outside of the history are counted");

			// Ground lines follow layers that are added, removed and moved, and come back with them
			SmallVector<TerrainSpline, 0> splines;
			TerrainSpline& spline = splines.emplace_back();
			spline.Layer = 2;
			spline.Points.push_back(Vector2f(0.0f, 0.0f));
			spline.Points.push_back(Vector2f(64.0f, 0.0f));
			spline.Owned.push_back(TerrainSpline::OwnedTile{ 1, 1, TileRef{ 3, TileRef::DefaultTileSet, 0 }, TileRef{} });
			TerrainSpline::Write(splines, data);
			TerrainSpline::SetChunkData(document, data);
			auto splineLayer = [&document]() {
				SmallVector<TerrainSpline, 0> read;
				TerrainSpline::Read(document, read);
				return (read.empty() ? -1 : read[0].Layer);
			};
			DocumentLayer layer;
			layer.Record = document.Layers[0].Record;
			layer.Record.Width = 4;
			layer.Record.Height = 4;
			layer.Tiles.resize(16, TileRef{});
			layer.EditorFlags = DocumentLayer::EditorLocked;
			history.Execute(InsertLayerCommand::Add(document, 0, layer, "Add layer"_s), changes);
			expect(splineLayer() == 3 && document.Layers[0].EditorFlags == DocumentLayer::EditorLocked, "added layer");
			history.Execute(InsertLayerCommand::Remove(document, 3, "Remove layer"_s), changes);
			expect(splineLayer() == -1, "a line goes with its layer");
			history.Undo(changes);
			expect(splineLayer() == 3, "a line comes back with its layer");
			history.Undo(changes);
			expect(splineLayer() == 2, "the layer is removed again");
			history.Execute(std::make_unique<MoveLayerCommand>(2, 0, "Move layer"_s), changes);
			expect(splineLayer() == 0, "a line moves with its layer");
			history.Undo(changes);
			expect(splineLayer() == 2, "a line moves back with its layer");

			if (problems == 0) {
				LOGI("State outside of the history test passed");
			}
			return problems;
		}

		// A recovery copy holds the level as it is, and it's gone once the changes are undone
		std::int32_t TestAutosaveRoundTrip()
		{
			std::int32_t problems = 0;
			auto expect = [&problems](bool condition, const char* what) {
				if (!condition) {
					LOGE("Autosave: {}", what);
					problems++;
				}
			};

			LevelDocument document;
			document.CreateNew("test"_s, 100, Vector2i(16, 8));
			CommandHistory history(document, SIZE_MAX);
			DocumentChanges changes;
			// A path with a character outside of ASCII, as the directory of the game has one
			String source = fs::CombinePath(fs::GetTempDirectory(), "selftest-autosave-\xC2\xB2.j2l"_s);
			String recoveryPath = AutosaveService::GetRecoveryPath(source);
			AutosaveService autosave;
			autosave.SetSource(source);
			autosave.Discard();

			// Waited for, the copy is written on a worker thread otherwise
			expect(autosave.SaveNow(document, history, true) && !fs::FileExists(recoveryPath), "nothing written without changes");
			auto command = std::make_unique<SetTilesCommand>(document, 0, "Tiles"_s);
			command->Set(3, 2, TileRef{ 7, TileRef::DefaultTileSet, 0 });
			history.Execute(Death::move(command), changes);
			expect(autosave.SaveNow(document, history, true) && fs::FileExists(recoveryPath), "written with changes");
			expect(document.FindChunk(AutosaveService::RecoveryChunk) == nullptr, "the document is left as it was");
			expect(AutosaveService::FindRecovery(source) == recoveryPath, "found again");

			LevelDocument recovered;
			{
				auto s = fs::Open(recoveryPath, FileAccess::Read);
				expect(s->IsValid() && recovered.Load(*s, nullptr), "the copy can be loaded");
			}
			const Formats::ChunkRecord* info = recovered.FindChunk(AutosaveService::RecoveryChunk);
			expect(info != nullptr && StringView(reinterpret_cast<const char*>(info->Data.data()), info->Data.size()) == source, "the copy knows its file");
			// The history of the changes is kept in the copy too, not a part of the level
			SmallVector<std::uint8_t, 0> archive;
			if (const Formats::ChunkRecord* chunk = recovered.FindChunk(CommandArchive::Chunk)) {
				archive = chunk->Data;
			}
			recovered.SetChunk(AutosaveService::RecoveryChunk, {});
			recovered.SetChunk(CommandArchive::Chunk, {});
			MemoryStream expected, actual;
			Export(document, expected, false);
			Export(recovered, actual, false);
			expect(AreEqual(expected, actual), "the copy holds the level");
			CommandHistory recoveredHistory(recovered, SIZE_MAX);
			expect(!archive.empty() && CommandArchive::Read(archive, recovered, recoveredHistory) && recoveredHistory.GetDoneCount() == 1,
				"the copy keeps the history");
			recoveredHistory.Undo(changes);
			expect(recovered.Layers[0].GetTile(3, 2).IsEmpty(), "a restored change undone");

			// Undone back to how it was saved, the copy would only offer the undone changes again
			history.Undo(changes);
			autosave.Update(1.0f, document, history);
			expect(!fs::FileExists(recoveryPath), "removed when all is undone");
			autosave.Discard();

			if (problems == 0) {
				LOGI("Autosave test passed");
			}
			return problems;
		}

		// Editor chunks are read back as they were written, those of 3.9.0 too, and the preview flag follows the chunk
		std::int32_t TestEditorChunks()
		{
			std::int32_t problems = 0;
			auto expect = [&problems](bool condition, const char* what) {
				if (!condition) {
					LOGE("Editor chunks: {}", what);
					problems++;
				}
			};
			auto sameSplines = [](const SmallVectorImpl<TerrainSpline>& a, const SmallVectorImpl<TerrainSpline>& b) {
				if (a.size() != b.size()) {
					return false;
				}
				for (std::size_t i = 0; i < a.size(); i++) {
					if (a[i].Layer != b[i].Layer || a[i].Depth != b[i].Depth || a[i].Kind != b[i].Kind || a[i].Smooth != b[i].Smooth ||
						a[i].Points.size() != b[i].Points.size() || a[i].Owned.size() != b[i].Owned.size()) {
						return false;
					}
					for (std::size_t j = 0; j < a[i].Points.size(); j++) {
						if (a[i].Points[j].X != b[i].Points[j].X || a[i].Points[j].Y != b[i].Points[j].Y) {
							return false;
						}
					}
					for (std::size_t j = 0; j < a[i].Owned.size(); j++) {
						const TerrainSpline::OwnedTile& x = a[i].Owned[j];
						const TerrainSpline::OwnedTile& y = b[i].Owned[j];
						if (x.X != y.X || x.Y != y.Y || x.Written != y.Written || x.Previous != y.Previous) {
							return false;
						}
					}
				}
				return true;
			};
			auto putVarUint = [](SmallVectorImpl<std::uint8_t>& data, std::uint32_t value) {
				Formats::WriteVarUint(data, value);
			};
			auto put32 = [](SmallVectorImpl<std::uint8_t>& data, std::uint32_t value) {
				for (std::int32_t i = 0; i < 4; i++) {
					data.push_back(std::uint8_t(value >> (i * 8)));
				}
			};

			LevelDocument document;
			document.CreateNew("test"_s, 100, Vector2i(16, 16));
			SmallVector<TerrainSpline, 0> splines;
			for (std::int32_t i = 0; i < 2; i++) {
				TerrainSpline& spline = splines.emplace_back();
				spline.Layer = i;
				spline.Depth = 3 + i;
				spline.Kind = (i == 0 ? SplineKind::Ground : SplineKind::Closed);
				spline.Smooth = (i == 1);
				spline.Points.push_back(Vector2f(1.5f, -2.0f));
				spline.Points.push_back(Vector2f(100.25f, 40.0f));
				spline.Owned.push_back(TerrainSpline::OwnedTile{ 2, 3, TileRef{ 9, TileRef::DefaultTileSet, 1 }, TileRef{ 4, TileRef::Animated, 0 } });
			}
			SmallVector<std::uint8_t, 0> data;
			TerrainSpline::Write(splines, data);
			TerrainSpline::SetChunkData(document, data);
			SmallVector<TerrainSpline, 0> read;
			TerrainSpline::Read(document, read);
			expect(sameSplines(splines, read), "ground lines read back");

			// The layout of 3.9.0, without a version and the sizes of the records
			SmallVector<std::uint8_t, 0> legacy;
			putVarUint(legacy, std::uint32_t(splines.size()));
			for (const TerrainSpline& spline : splines) {
				put32(legacy, std::uint32_t(spline.Layer));
				legacy.push_back(std::uint8_t(spline.Depth));
				legacy.push_back(std::uint8_t(spline.Kind));
				legacy.push_back(spline.Smooth ? 1 : 0);
				putVarUint(legacy, std::uint32_t(spline.Points.size()));
				for (const Vector2f& point : spline.Points) {
					std::uint32_t bits;
					std::memcpy(&bits, &point.X, sizeof(bits));
					put32(legacy, bits);
					std::memcpy(&bits, &point.Y, sizeof(bits));
					put32(legacy, bits);
				}
				putVarUint(legacy, std::uint32_t(spline.Owned.size()));
				for (const TerrainSpline::OwnedTile& owned : spline.Owned) {
					put32(legacy, std::uint32_t(owned.X));
					put32(legacy, std::uint32_t(owned.Y));
					put32(legacy, std::uint32_t(owned.Written.Index) | (std::uint32_t(owned.Written.Source) << 16) | (std::uint32_t(owned.Written.Flags) << 24));
					put32(legacy, std::uint32_t(owned.Previous.Index) | (std::uint32_t(owned.Previous.Source) << 16) | (std::uint32_t(owned.Previous.Flags) << 24));
				}
			}
			document.SetChunk(TerrainSpline::Chunk, {});
			document.SetChunk(TerrainSpline::LegacyChunk, legacy);
			TerrainSpline::UpgradeLegacyChunk(document);
			TerrainSpline::Read(document, read);
			expect(sameSplines(splines, read) && document.FindChunk(TerrainSpline::LegacyChunk) == nullptr, "ground lines of 3.9.0 upgraded");

			SmallVector<std::uint8_t, 0> legacyNotes;
			putVarUint(legacyNotes, 1);
			put32(legacyNotes, std::uint32_t(-12));
			put32(legacyNotes, 34);
			legacyNotes.push_back(3);
			putVarUint(legacyNotes, 4);
			for (char c : "Home"_s) {
				legacyNotes.push_back(std::uint8_t(c));
			}
			document.SetChunk(Annotation::LegacyChunk, legacyNotes);
			Annotation::UpgradeLegacyChunk(document);
			SmallVector<Annotation, 0> annotations;
			Annotation::Read(document, annotations);
			expect(annotations.size() == 1 && annotations[0].X == -12 && annotations[0].Y == 34 && annotations[0].Slot == 3 && annotations[0].Text == "Home"_s &&
				document.FindChunk(Annotation::LegacyChunk) == nullptr, "bookmarks of 3.9.0 upgraded");

			// A note of version 2, with its color, author, time, state and replies
			{
				SmallVector<Annotation, 0> notes;
				Annotation& note = notes.emplace_back();
				note.X = 5;
				note.Y = 6;
				note.Text = "Fix"_s;
				note.Color = 0xFF112233u;
				note.Author = "Dan"_s;
				note.Time = 1700000000;
				note.Resolved = true;
				Annotation::Reply& reply = note.Replies.emplace_back();
				reply.Author = "Bob"_s;
				reply.Time = 1700000100;
				reply.Text = "Done"_s;
				SmallVector<std::uint8_t, 0> noteData;
				Annotation::Serialize(notes, noteData);
				document.SetChunk(Annotation::Chunk, noteData);
				SmallVector<Annotation, 0> readNotes;
				Annotation::Read(document, readNotes);
				expect(readNotes.size() == 1 && readNotes[0].Color == note.Color && readNotes[0].Author == note.Author && readNotes[0].Time == note.Time &&
					readNotes[0].Resolved && readNotes[0].Replies.size() == 1 && readNotes[0].Replies[0].Text == "Done"_s &&
					readNotes[0].Replies[0].Time == 1700000100, "notes with replies");
				// The bookmark of before is put back for the rest of the test
				Annotation::Write(document, annotations);
			}

			// A line with points that make no sense is left out, the others are kept
			splines[0].Points[1].X = std::numeric_limits<float>::quiet_NaN();
			TerrainSpline::Write(splines, data);
			TerrainSpline::SetChunkData(document, data);
			TerrainSpline::Read(document, read);
			expect(read.size() == 1 && read[0].Layer == 1, "a damaged line left out");
			splines[0].Points[1].X = 100.25f;
			TerrainSpline::Write(splines, data);
			TerrainSpline::SetChunkData(document, data);

			// Through a file, with the editor state of the layers and a preview
			static const std::uint8_t Preview[] = { 1, 0, 1, 0, 10, 20, 30 };
			document.Layers[1].EditorFlags = DocumentLayer::EditorHidden | DocumentLayer::EditorLocked;
			document.Layers[1].EditorOpacity = 128;
			// How the sprite layer is drawn isn't in its record, it goes through a chunk of the game
			static const std::uint8_t SpriteLayerColor[4] = { 64, 255, 255, 192 };
			document.Layers[1].Record.RendererType = std::uint8_t(Tiles::LayerRendererType::PaletteShift);
			std::memcpy(document.Layers[1].Record.Color, SpriteLayerColor, sizeof(SpriteLayerColor));
			// Neither are the parameters of a reflection its color has no room for
			document.Layers[0].Record.RendererType = std::uint8_t(Tiles::LayerRendererType::Reflection);
			document.Layers[0].Record.Textured.FadeX = 0.25f;
			document.Layers[0].Record.Textured.FadeY = 0.75f;
			document.Layers[0].Record.Textured.Fade = 28;
			// Nor its inner speeds and a texture of its own, as one of JJ2+ a converted level has
			document.Layers[0].Record.Textured.Flags = Formats::TexturedLayerParams::InnerLayer;
			document.Layers[0].Record.Textured.InnerSpeedX = 0.3f;
			document.Layers[0].Record.Textured.InnerSpeedY = -0.5f;
			document.Layers[0].Record.Textured.InnerAutoSpeedX = 0.8f;
			document.Layers[0].Record.Textured.InnerAutoSpeedY = 1.25f;
			constexpr std::size_t TextureBytes = std::size_t(Formats::TexturedLayerChunk::TextureSize) * Formats::TexturedLayerChunk::TextureSize;
			document.Layers[0].Texture.resize_for_overwrite(TextureBytes);
			for (std::size_t i = 0; i < TextureBytes; i++) {
				document.Layers[0].Texture[i] = std::uint8_t(176 + (i * 7 + i / 256) % 32);
			}
			auto sameTexture = [&document](const DocumentLayer& layer) {
				return (layer.Texture.size() == document.Layers[0].Texture.size() &&
					std::equal(layer.Texture.begin(), layer.Texture.end(), document.Layers[0].Texture.begin()));
			};
			{
				// Kept by a step that puts the layer back too
				CompressedBuffer compressed;
				DocumentSnapshot::CompressLayer(document.Layers[0], compressed);
				DocumentLayer restored;
				expect(DocumentSnapshot::DecompressLayer(compressed, restored) && sameTexture(restored) &&
					restored.Record.Textured.InnerAutoSpeedY == 1.25f, "the texture of a layer through a snapshot");
			}
			document.SetChunk(Formats::LevelFile::PreviewChunk, Preview);
			MemoryStream saved;
			expect(document.Save(saved, false, 1), "saved");
			saved.Seek(0, SeekOrigin::Begin);
			Formats::LevelHeader header;
			expect(Formats::LevelFile::ReadHeader(saved, header) && (header.Flags & LevelFlags::HasPreview) == LevelFlags::HasPreview, "the preview flag is set with the preview");
			saved.Seek(0, SeekOrigin::Begin);
			LevelDocument loaded;
			expect(loaded.Load(saved, nullptr), "loaded");
			TerrainSpline::Read(loaded, read);
			Annotation::Read(loaded, annotations);
			expect(sameSplines(splines, read) && annotations.size() == 1 && loaded.Layers[1].EditorFlags == document.Layers[1].EditorFlags,
				"editor chunks through a file");
			expect(loaded.Layers[1].EditorOpacity == 128 && loaded.Layers[0].EditorOpacity == 255, "the opacity of layers through a file");
			expect((loaded.Flags & LevelFlags::HasPreview) == LevelFlags::None, "the preview flag isn't kept in the document");
			expect(loaded.Layers[1].Record.RendererType == document.Layers[1].Record.RendererType &&
				std::memcmp(loaded.Layers[1].Record.Color, SpriteLayerColor, sizeof(SpriteLayerColor)) == 0 &&
				loaded.FindChunk(Formats::SpriteLayerChunk::Tag) == nullptr, "how the sprite layer is drawn through a file");
			const Formats::TexturedLayerParams& textured = loaded.Layers[0].Record.Textured;
			expect(textured.FadeX == 0.25f && textured.FadeY == 0.75f && textured.Fade == 28 && textured.Flags == Formats::TexturedLayerParams::InnerLayer &&
				loaded.Layers[1].Record.Textured.IsDefault() && loaded.FindChunk(Formats::TexturedLayerChunk::Tag) == nullptr,
				"the parameters of a reflection through a file");
			expect(textured.InnerSpeedX == 0.3f && textured.InnerSpeedY == -0.5f && textured.InnerAutoSpeedX == 0.8f && textured.InnerAutoSpeedY == 1.25f &&
				sameTexture(loaded.Layers[0]) && loaded.Layers[1].Texture.empty(), "the inner speeds and the texture of a reflection through a file");

			document.SetChunk(Formats::LevelFile::PreviewChunk, {});
			MemoryStream noPreview;
			expect(document.Save(noPreview, true, 1), "exported");
			noPreview.Seek(0, SeekOrigin::Begin);
			expect(Formats::LevelFile::ReadHeader(noPreview, header) && (header.Flags & LevelFlags::HasPreview) == LevelFlags::None, "no preview flag without the preview");
			noPreview.Seek(0, SeekOrigin::Begin);
			LevelDocument exported;
			expect(exported.Load(noPreview, nullptr) && exported.Layers[1].Record.RendererType == document.Layers[1].Record.RendererType,
				"how the sprite layer is drawn kept by an export");
			expect(exported.Layers[0].Record.Textured.Fade == 28 && exported.Layers[0].Record.Textured.InnerAutoSpeedX == 0.8f &&
				sameTexture(exported.Layers[0]), "the parameters and the texture of a reflection kept by an export");

			// An entry of before, which ends after the flags, has no inner speeds and takes the tiles
			{
				static const std::uint8_t ShortEntry[] = { 1, 0, 10, 0x00, 0x00, 0x80, 0x3E, 0x00, 0x00, 0x40, 0x3F, 28, 0x04 };
				SmallVector<Formats::TexturedLayerChunk::Entry, 1> entries;
				expect(Formats::TexturedLayerChunk::Read(ShortEntry, entries) && entries.size() == 1 && entries[0].Params.FadeX == 0.25f &&
					entries[0].Params.FadeY == 0.75f && entries[0].Params.HasDefaultInnerSpeeds() && entries[0].Texture.empty(),
					"an entry of the parameters of a reflection of before");
				SmallVector<std::uint8_t, 0> written;
				Formats::TexturedLayerChunk::Write(entries, written);
				expect(written.size() == sizeof(ShortEntry) && std::equal(written.begin(), written.end(), ShortEntry),
					"an entry without inner speeds and a texture written as before");
			}

			if (problems == 0) {
				LOGI("Editor chunks test passed");
			}
			return problems;
		}

		// Many events set at once by a single pass end up as when they're set one by one
		std::int32_t TestEventMerge()
		{
			LevelDocument byOne;
			byOne.CreateNew("test"_s, 100, Vector2i(40, 30));
			TestRandom random(73);
			auto makeEvent = [&random]() {
				Formats::EventRecord event = LevelDocument::GetEmptyEvent();
				if (random.Next(4) != 0) {
					event.Type = std::uint16_t(1 + random.Next(300));
					event.Params[0] = std::uint8_t(random.Next(256));
				}
				return event;
			};
			for (std::int32_t i = 0; i < 300; i++) {
				byOne.SetEvent(random.Next(40), random.Next(30), makeEvent());
			}
			LevelDocument atOnce = byOne;

			SmallVector<DocumentEvent, 0> events;
			for (std::uint32_t cell = 0; cell < 40 * 30; cell++) {
				if (random.Next(5) == 0) {
					DocumentEvent& event = events.emplace_back();
					event.Cell = cell;
					event.Record = makeEvent();
				}
			}
			for (const DocumentEvent& event : events) {
				byOne.SetEvent(std::int32_t(event.Cell % 40), std::int32_t(event.Cell / 40), event.Record);
			}
			atOnce.SetEvents(events);

			bool same = (byOne.Events.size() == atOnce.Events.size());
			for (std::size_t i = 0; same && i < byOne.Events.size(); i++) {
				same = (byOne.Events[i].Cell == atOnce.Events[i].Cell && LevelDocument::AreEventsEqual(byOne.Events[i].Record, atOnce.Events[i].Record));
			}
			if (!same) {
				LOGE("Event merge: events set at once differ from those set one by one");
				return 1;
			}
			LOGI("Event merge test passed");
			return 0;
		}

		// The history kept in a recovery copy undoes and redoes the same as the history it was made of
		std::int32_t TestCommandArchive()
		{
			std::int32_t problems = 0;
			auto expect = [&problems](bool condition, const char* what) {
				if (!condition) {
					LOGE("Command archive: {}", what);
					problems++;
				}
			};

			LevelDocument document;
			document.CreateNew("test"_s, 100, Vector2i(24, 16));
			CommandHistory history(document, SIZE_MAX);
			DocumentChanges changes;
			TestRandom random(123);
			for (std::int32_t i = 0; i < 60; i++) {
				ApplyRandomCommand(random, history, changes);
			}
			// A change of a chunk and of the off-grid events too, the random ones don't make those
			static const std::uint8_t ChunkData[] = { 7, 8, 9 };
			history.Execute(std::make_unique<SetChunkCommand>(document, Formats::LevelFile::MakeChunkTag('e', 't', 's', 't'), ChunkData, "Chunk"_s), changes);
			// A texture of its own of a layer given and taken away again, the random ones don't change those either
			constexpr std::size_t TextureBytes = std::size_t(Formats::TexturedLayerChunk::TextureSize) * std::size_t(Formats::TexturedLayerChunk::TextureSize);
			SmallVector<std::uint8_t, 0> texture;
			texture.resize_for_overwrite(TextureBytes);
			for (std::size_t i = 0; i < texture.size(); i++) {
				texture[i] = std::uint8_t(176 + (i * 13 + i / 256) % 32);
			}
			history.Execute(std::make_unique<SetLayerTextureCommand>(document, 0, Death::move(texture), "Texture"_s), changes);
			history.Execute(std::make_unique<SetLayerTextureCommand>(document, 0, SmallVector<std::uint8_t, 0>(), "No texture"_s), changes);
			SmallVector<std::uint8_t, 0> flatTexture;
			flatTexture.resize_for_overwrite(TextureBytes);
			std::memset(flatTexture.data(), 180, flatTexture.size());
			history.Execute(std::make_unique<SetLayerTextureCommand>(document, 0, Death::move(flatTexture), "Texture again"_s), changes);

			SmallVector<std::uint8_t, 0> archive;
			CommandArchive::Write(history, archive);
			expect(!archive.empty(), "the history is written");

			LevelDocument restored = document;
			CommandHistory restoredHistory(restored, SIZE_MAX);
			expect(CommandArchive::Read(archive, restored, restoredHistory), "the history is read");
			expect(restoredHistory.GetCount() == history.GetCount() && restoredHistory.GetDoneCount() == history.GetDoneCount(), "as many changes");

			auto same = [&]() {
				MemoryStream a, b;
				Export(document, a, false);
				Export(restored, b, false);
				return AreEqual(a, b);
			};
			std::int32_t steps = 0;
			while (history.Undo(changes)) {
				restoredHistory.Undo(changes);
				if (!same()) {
					LOGE("Command archive: the level differs after {} steps undone", steps + 1);
					problems++;
					break;
				}
				steps++;
			}
			expect(!restoredHistory.CanUndo(), "nothing more to undo");
			while (history.Redo(changes)) {
				restoredHistory.Redo(changes);
				if (!same()) {
					LOGE("Command archive: the level differs after a step redone");
					problems++;
					break;
				}
			}

			// A damaged history isn't restored at all
			if (archive.size() > 8) {
				archive[archive.size() / 2] ^= 0xFF;
				archive.truncate(archive.size() - 3);
				LevelDocument damaged = document;
				CommandHistory damagedHistory(damaged, SIZE_MAX);
				expect(!CommandArchive::Read(archive, damaged, damagedHistory) && damagedHistory.GetCount() == 0, "a damaged history is refused");
			}

			if (problems == 0) {
				LOGI("Command archive test passed ({} steps)", steps);
			}
			return problems;
		}

		// A ZIP archive has its files where its directory says, and they're read back as they were
		std::int32_t TestZipWriter()
		{
			std::int32_t problems = 0;
			auto expect = [&problems](bool condition, const char* what) {
				if (!condition) {
					LOGE("ZIP writer: {}", what);
					problems++;
				}
			};
			static const char Check[] = "123456789";
			expect(ZipWriter::Crc32(ArrayView<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(Check), 9)) == 0xCBF43926u, "CRC-32");

			SmallVector<std::uint8_t, 0> text;
			for (std::int32_t i = 0; i < 200; i++) {
				text.push_back(std::uint8_t('a' + (i % 3)));
			}
			MemoryStream zip(4096);
			{
				ZipWriter writer(zip);
				expect(writer.AddFile("dir/text.txt"_s, text), "a file added");
				expect(writer.AddFile("empty.bin"_s, {}), "an empty file added");
				expect(writer.Finish(), "finished");
			}
			const std::uint8_t* data = zip.GetBuffer();
			std::int64_t size = zip.GetSize();
			auto read16 = [data](std::int64_t pos) { return std::uint32_t(data[pos] | (data[pos + 1] << 8)); };
			auto read32 = [&read16](std::int64_t pos) { return read16(pos) | (read16(pos + 2) << 16); };
			expect(size > 22 && read32(size - 22) == 0x06054B50u && read16(size - 22 + 10) == 2, "the end of the directory");
			expect(read32(0) == 0x04034B50u, "the first file");
			if (problems == 0) {
				std::uint32_t method = read16(8), compressedSize = read32(18), originalSize = read32(22), nameLength = read16(26);
				const std::uint8_t* content = data + 30 + nameLength;
				SmallVector<std::uint8_t, 0> unpacked;
				unpacked.resize_for_overwrite(originalSize);
				if (method == 8) {
					MemoryStream compressed(content, std::int64_t(compressedSize));
					Death::IO::Compression::DeflateStream inflate(compressed, std::int32_t(compressedSize));
					expect(inflate.Read(unpacked.data(), originalSize) == std::int64_t(originalSize), "the file inflated");
				} else {
					std::memcpy(unpacked.data(), content, originalSize);
				}
				expect(originalSize == text.size() && std::memcmp(unpacked.data(), text.data(), text.size()) == 0, "the file read back");
				expect(read32(14) == ZipWriter::Crc32(text), "the checksum of the file");
			}
			if (problems == 0) {
				LOGI("ZIP writer test passed");
			}
			return problems;
		}

		// Catalogs for translators and the translations a level carries are read back as they were written
		std::int32_t TestLevelTextTranslations()
		{
			std::int32_t problems = 0;
			auto expect = [&problems](bool condition, const char* what) {
				if (!condition) {
					LOGE("Level text translations: {}", what);
					problems++;
				}
			};

			SmallVector<PoEntry, 0> entries;
			PoEntry& first = entries.emplace_back();
			first.Context = "0"_s;
			first.Id = "Hello \"world\"\nsecond line\f"_s;
			first.Translation = "Ahoj \\ svete\ndruhy radek"_s;
			PoEntry& second = entries.emplace_back();
			second.Context = "3"_s;
			second.Id = "Bye|Part"_s;
			second.Translation = "Nashle|Cast"_s;
			String text = PoFile::Write(entries, "cs"_s, "Test"_s);
			SmallVector<PoEntry, 0> read;
			String language;
			expect(PoFile::Read(text, read, language) && language == "cs"_s && read.size() == 2, "a catalog read back");
			for (std::size_t i = 0; i < read.size() && i < entries.size(); i++) {
				expect(read[i].Context == entries[i].Context && read[i].Id == entries[i].Id && read[i].Translation == entries[i].Translation, "an entry read back");
			}

			SmallVector<LocalizedLevelTexts, 0> languages;
			LocalizedLevelTexts& portuguese = languages.emplace_back();
			portuguese.Language = "pt"_s;
			portuguese.Texts.resize(3);
			portuguese.Texts[2] = "Ola"_s;
			LocalizedLevelTexts& empty = languages.emplace_back();
			empty.Language = "de"_s;
			empty.Texts.resize(2);
			SmallVector<std::uint8_t, 0> data;
			Formats::LevelTextsChunk::Write(ArrayView<const LocalizedLevelTexts>(languages.data(), languages.size()), data);
			SmallVector<LocalizedLevelTexts, 0> readLanguages;
			expect(Formats::LevelTextsChunk::Read(data, readLanguages) && readLanguages.size() == 1, "a language without translations is left out");
			const LocalizedLevelTexts* found = Formats::LevelTextsChunk::Find(ArrayView<const LocalizedLevelTexts>(readLanguages.data(), readLanguages.size()), "pt-BR"_s);
			expect(found != nullptr && found->Texts.size() == 3 && found->Texts[2] == "Ola"_s && found->Texts[0].empty(), "a language found without its region");
			expect(Formats::LevelTextsChunk::Find(ArrayView<const LocalizedLevelTexts>(readLanguages.data(), readLanguages.size()), "cs"_s) == nullptr, "a language not there");

			if (problems == 0) {
				LOGI("Level text translations test passed");
			}
			return problems;
		}

		// An episode file is read back as it was written, with its images
		std::int32_t TestEpisodeFile()
		{
			EpisodeFile episode;
			episode.DisplayName = "Test Episode"_s;
			episode.Position = 123;
			episode.FirstLevel = "first"_s;
			episode.PreviousEpisode = "prev"_s;
			episode.NextEpisode = "next"_s;
			episode.Title.Width = 3;
			episode.Title.Height = 2;
			for (std::int32_t i = 0; i < 3 * 2 * 4; i++) {
				episode.Title.Pixels.push_back(std::uint8_t(i * 11));
			}
			String path = fs::CombinePath(fs::GetTempDirectory(), "selftest-episode.j2e"_s);
			EpisodeFile read;
			bool ok = episode.Write(path, false) && EpisodeFile::Read(path, read);
			fs::RemoveFile(path);
			ok = ok && read.DisplayName == episode.DisplayName && read.Position == episode.Position && read.FirstLevel == episode.FirstLevel &&
				read.PreviousEpisode == episode.PreviousEpisode && read.NextEpisode == episode.NextEpisode && read.Title.Width == 3 && read.Title.Height == 2 &&
				read.Title.Pixels.size() == episode.Title.Pixels.size() && std::memcmp(read.Title.Pixels.data(), episode.Title.Pixels.data(), read.Title.Pixels.size()) == 0 &&
				read.Background.IsEmpty();
			String level;
			ok = ok && SplitLevelReference("Ep/Level1"_s, level) == "ep"_s && level == "level1"_s && SplitLevelReference("solo"_s, level).empty() && level == "solo"_s;
			if (!ok) {
				LOGE("Episode file: the episode isn't the same after it was written and read");
				return 1;
			}
			LOGI("Episode file test passed");
			return 0;
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

		// The tests that need no game data, only the temporary directory and the directory of the settings, returns the
		// number of problems found
		std::int32_t RunUnitTests()
		{
			std::int32_t problems = TestCatalogConsistency();
			problems += TestCatalogParams();
			problems += TestTileSetRemap();
			problems += TestTileIdSpaceGrowth();
			problems += TestEventFlags();
			problems += TestLayerResize();
			problems += TestPngCodec();
			problems += TestTileSetImages();
			problems += TestQuantizer();
			problems += TestPolylineSplit();
			problems += TestPaletteFiles();
			problems += TestFxTiles();
			problems += TestViewState();
			problems += TestLayerPlacement();
			problems += TestTileSetOrder();
			problems += TestHistoryBudget();
			problems += TestMergePolicy();
			problems += TestOutsideHistoryState();
			problems += TestAutosaveRoundTrip();
			problems += TestEditorChunks();
			problems += TestEventMerge();
			problems += TestCommandArchive();
			problems += TestZipWriter();
			problems += TestLevelTextTranslations();
			problems += TestEpisodeFile();
			problems += ToolActions::SelfTest();
			problems += PlayPathHistory::SelfTest();
			problems += ActionRegistry::SelfTest();
			problems += ToolTests::TestTools();
			problems += ToolTests::TestRuntimeSync();
			return problems;
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

		std::int32_t catalogProblems = RunUnitTests();

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

	bool EditorSelfTest::RunUnit()
	{
		std::int32_t problems = RunUnitTests();
		LOGI("Editor unit tests finished: {} problems", problems);
		return (problems == 0);
	}
}

#endif
