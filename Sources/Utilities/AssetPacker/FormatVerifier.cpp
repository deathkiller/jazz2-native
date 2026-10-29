#include "FormatVerifier.h"

#include "../../Jazz2/Formats/LevelFile.h"
#include "../../Jazz2/Formats/LevelTextsChunk.h"
#include "../../Jazz2/Formats/TileSetFile.h"
#include "../../Jazz2/Formats/WeaponsChunk.h"

#include <Containers/String.h>
#include <Containers/StringConcatenable.h>
#include <Core/ITraceSink.h>
#include <IO/FileSystem.h>
#include <IO/MemoryStream.h>
#include <IO/Compression/DeflateStream.h>
#include <Utf8.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>

#if !defined(DEATH_TARGET_EMSCRIPTEN)
#	include <thread>
#endif
#if defined(DEATH_TARGET_WINDOWS)
#	include <CommonWindows.h>
#	include <psapi.h>
#endif

using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::IO;
using namespace Death::IO::Compression;
using namespace Jazz2::Formats;

namespace Jazz2::AssetPacker
{
	namespace
	{
		enum class VerifyResult {
			Identical,
			Equivalent,
			Different,
			NotNative,
			Failed
		};

		struct LayerData {
			LayerRecord Header;
			SmallVector<TileRecord, 0> Tiles;
		};

		/** Everything a level file holds, as the records the reader returns */
		struct LevelData {
			LevelHeader Header;
			LevelProperties Properties;
			bool HasLevelPalette;
			std::uint32_t LevelPalette[LevelFile::ColorsPerPalette];
			SmallVector<PaletteRecord, 0> AdditionalPalettes;
			SmallVector<ExtraTileSetRecord, 0> ExtraTileSets;
			SmallVector<TileDiffuseOverride, 0> DiffuseOverrides;
			SmallVector<TileMaskOverride, 0> MaskOverrides;
			SmallVector<String, 0> Texts;
			std::uint16_t AnimatedTilesOffset;
			SmallVector<AnimatedTileRecord, 0> AnimatedTiles;
			SmallVector<LayerData, 0> Layers;
			SmallVector<EventRecord, 0> Events;
			SmallVector<OffGridEventRecord, 0> OffGridEvents;
			SmallVector<ChunkRecord, 0> Chunks;
			/** Why the level couldn't be read, as the reader told */
			StringView Error;
		};

		enum class ReadResult {
			Success,
			NotNative,
			Failed
		};

		ReadResult ReadLevel(const std::uint8_t* data, std::int64_t size, LevelData& level)
		{
			MemoryStream input(data, size);
			if (!LevelFile::ReadHeader(input, level.Header)) {
				return ReadResult::NotNative;
			}

			LevelReader reader(input, level.Header);
			reader.ReadProperties(level.Properties);
			level.HasLevelPalette = reader.ReadPalettes(level.LevelPalette, &level.AdditionalPalettes);
			reader.ReadExtraTileSets(level.ExtraTileSets);
			reader.ReadTileDiffuseOverrides(&level.DiffuseOverrides);
			reader.ReadTileMaskOverrides(&level.MaskOverrides);
			reader.ReadTexts(level.Texts);
			reader.ReadAnimatedTiles(level.AnimatedTilesOffset, level.AnimatedTiles);

			std::int32_t layerCount = reader.ReadLayerCount();
			level.Layers.reserve(layerCount);
			const LayerData* spriteLayer = nullptr;
			for (std::int32_t i = 0; i < layerCount && reader.IsValid(); i++) {
				LayerData& layer = level.Layers.emplace_back();
				reader.ReadLayerHeader(layer.Header);
				layer.Tiles.resize_for_overwrite(std::size_t(layer.Header.Width) * std::size_t(layer.Header.Height));
				reader.ReadLayerTiles(layer.Header, [&layer](std::int32_t first, ArrayView<const TileRecord> tiles) {
					std::memcpy(&layer.Tiles[first], tiles.data(), tiles.size() * sizeof(TileRecord));
				});
				if (layer.Header.Kind == LayerKind::Sprite) {
					spriteLayer = &layer;
				}
			}

			if (spriteLayer == nullptr) {
				level.Error = (reader.IsValid() ? "The level has no sprite layer"_s : reader.GetError());
				return ReadResult::Failed;
			}

			// Not reserved up front, the events grow with the data that really comes (a damaged body can claim any size)
			reader.ReadEvents(spriteLayer->Header.Width, spriteLayer->Header.Height, [&level](std::int32_t, std::int32_t, const EventRecord& event) {
				level.Events.push_back(event);
			});
			reader.ReadOffGridEvents(level.OffGridEvents);
			reader.ReadChunks(level.Chunks, true);

			if (!reader.IsValid()) {
				level.Error = reader.GetError();
				return ReadResult::Failed;
			}
			return ReadResult::Success;
		}

		bool WriteLevel(Stream& output, const LevelData& level)
		{
			LevelWriter writer(output, level.Header.Flags);
			writer.WriteProperties(level.Properties);
			writer.WritePalettes(level.HasLevelPalette ? level.LevelPalette : nullptr, level.AdditionalPalettes);
			writer.WriteExtraTileSets(level.ExtraTileSets);
			writer.WriteTileDiffuseOverrides(level.DiffuseOverrides);
			writer.WriteTileMaskOverrides(level.MaskOverrides);
			writer.WriteTexts(level.Texts);
			writer.WriteAnimatedTiles(level.AnimatedTilesOffset, level.AnimatedTiles);
			writer.WriteLayerCount(std::int32_t(level.Layers.size()));
			for (const LayerData& layer : level.Layers) {
				writer.WriteLayerHeader(layer.Header);
				writer.WriteLayerTiles(layer.Tiles);
			}
			for (const EventRecord& event : level.Events) {
				writer.WriteEvent(event);
			}
			writer.WriteOffGridEvents(level.OffGridEvents);
			writer.WriteChunks(level.Chunks, false);
			return writer.Finish();
		}

		bool EventsEqual(const EventRecord& a, const EventRecord& b)
		{
			// NoParams is derived from the parameters by the writer, so another writer may have left it out, and
			// the writer never stores the parameters of an event without a type
			return (a.Type == b.Type && (a.Flags & ~EventRecord::NoParams) == (b.Flags & ~EventRecord::NoParams) &&
				a.GeneratorFlags == b.GeneratorFlags && a.GeneratorDelay == b.GeneratorDelay &&
				(a.Type == 0 || std::memcmp(a.Params, b.Params, sizeof(a.Params)) == 0));
		}

		bool TilesEqual(ArrayView<const TileRecord> a, ArrayView<const TileRecord> b)
		{
			if (a.size() != b.size()) {
				return false;
			}
			for (std::size_t i = 0; i < a.size(); i++) {
				if (a[i].Flags != b[i].Flags || a[i].TileId != b[i].TileId) {
					return false;
				}
			}
			return true;
		}

		/** Compares two levels record by record, returns the name of the first section that differs, or an empty view */
		StringView FindDifference(const LevelData& a, const LevelData& b)
		{
			// The preview flag is derived from the chunks by the writer, a level saved before it was known lacks it
			if ((a.Header.Flags & ~LevelFlags::HasPreview) != (b.Header.Flags & ~LevelFlags::HasPreview)) {
				return "header"_s;
			}

			const LevelProperties& pa = a.Properties;
			const LevelProperties& pb = b.Properties;
			if (pa.DisplayName != pb.DisplayName || pa.Author != pb.Author || pa.NextLevel != pb.NextLevel || pa.SecretLevel != pb.SecretLevel ||
				pa.BonusLevel != pb.BonusLevel || pa.DefaultTileSet != pb.DefaultTileSet || pa.Music != pb.Music ||
				pa.AmbientColor != pb.AmbientColor || pa.Weather != pb.Weather || pa.WeatherIntensity != pb.WeatherIntensity ||
				pa.WaterLevel != pb.WaterLevel || pa.CaptionTileId != pb.CaptionTileId) {
				return "properties"_s;
			}

			if (a.HasLevelPalette != b.HasLevelPalette || (a.HasLevelPalette && std::memcmp(a.LevelPalette, b.LevelPalette, sizeof(a.LevelPalette)) != 0) ||
				a.AdditionalPalettes.size() != b.AdditionalPalettes.size()) {
				return "palettes"_s;
			}
			for (std::size_t i = 0; i < a.AdditionalPalettes.size(); i++) {
				if (a.AdditionalPalettes[i].Name != b.AdditionalPalettes[i].Name ||
					std::memcmp(a.AdditionalPalettes[i].Colors, b.AdditionalPalettes[i].Colors, sizeof(a.AdditionalPalettes[i].Colors)) != 0) {
					return "palettes"_s;
				}
			}

			if (a.ExtraTileSets.size() != b.ExtraTileSets.size()) {
				return "extra tile sets"_s;
			}
			for (std::size_t i = 0; i < a.ExtraTileSets.size(); i++) {
				const ExtraTileSetRecord& ta = a.ExtraTileSets[i];
				const ExtraTileSetRecord& tb = b.ExtraTileSets[i];
				if (ta.Flags != tb.Flags || ta.Name != tb.Name || ta.Offset != tb.Offset || ta.Count != tb.Count ||
					ta.AlternatePalette != tb.AlternatePalette || std::memcmp(ta.Remapping, tb.Remapping, sizeof(ta.Remapping)) != 0 ||
					ta.Data.size() != tb.Data.size() || (!ta.Data.empty() && std::memcmp(ta.Data.data(), tb.Data.data(), ta.Data.size()) != 0)) {
					return "extra tile sets"_s;
				}
			}

			if (a.DiffuseOverrides.size() != b.DiffuseOverrides.size() || a.MaskOverrides.size() != b.MaskOverrides.size()) {
				return "tile overrides"_s;
			}
			for (std::size_t i = 0; i < a.DiffuseOverrides.size(); i++) {
				if (a.DiffuseOverrides[i].TileId != b.DiffuseOverrides[i].TileId ||
					std::memcmp(a.DiffuseOverrides[i].Indices, b.DiffuseOverrides[i].Indices, sizeof(a.DiffuseOverrides[i].Indices)) != 0) {
					return "tile overrides"_s;
				}
			}
			for (std::size_t i = 0; i < a.MaskOverrides.size(); i++) {
				if (a.MaskOverrides[i].TileId != b.MaskOverrides[i].TileId ||
					std::memcmp(a.MaskOverrides[i].Mask, b.MaskOverrides[i].Mask, sizeof(a.MaskOverrides[i].Mask)) != 0) {
					return "tile overrides"_s;
				}
			}

			if (a.Texts.size() != b.Texts.size()) {
				return "texts"_s;
			}
			for (std::size_t i = 0; i < a.Texts.size(); i++) {
				if (a.Texts[i] != b.Texts[i]) {
					return "texts"_s;
				}
			}

			if (a.AnimatedTilesOffset != b.AnimatedTilesOffset || a.AnimatedTiles.size() != b.AnimatedTiles.size()) {
				return "animated tiles"_s;
			}
			for (std::size_t i = 0; i < a.AnimatedTiles.size(); i++) {
				const AnimatedTileRecord& ta = a.AnimatedTiles[i];
				const AnimatedTileRecord& tb = b.AnimatedTiles[i];
				if (ta.FrameDuration16 != tb.FrameDuration16 || ta.Delay != tb.Delay || ta.DelayJitter != tb.DelayJitter ||
					ta.PingPongDelay != tb.PingPongDelay || ta.IsPingPong != tb.IsPingPong || !TilesEqual(ta.Frames, tb.Frames)) {
					return "animated tiles"_s;
				}
			}

			if (a.Layers.size() != b.Layers.size()) {
				return "layers"_s;
			}
			for (std::size_t i = 0; i < a.Layers.size(); i++) {
				const LayerRecord& la = a.Layers[i].Header;
				const LayerRecord& lb = b.Layers[i].Header;
				if (la.Kind != lb.Kind || la.Flags != lb.Flags || la.Width != lb.Width || la.Height != lb.Height ||
					la.SpeedModels != lb.SpeedModels || la.OffsetX != lb.OffsetX || la.OffsetY != lb.OffsetY ||
					la.SpeedX != lb.SpeedX || la.SpeedY != lb.SpeedY || la.AutoSpeedX != lb.AutoSpeedX ||
					la.AutoSpeedY != lb.AutoSpeedY || la.Depth != lb.Depth || la.RendererType != lb.RendererType ||
					std::memcmp(la.Color, lb.Color, sizeof(la.Color)) != 0 || !TilesEqual(a.Layers[i].Tiles, b.Layers[i].Tiles)) {
					return "layers"_s;
				}
			}

			if (a.Events.size() != b.Events.size()) {
				return "events"_s;
			}
			for (std::size_t i = 0; i < a.Events.size(); i++) {
				if (!EventsEqual(a.Events[i], b.Events[i])) {
					return "events"_s;
				}
			}

			if (a.OffGridEvents.size() != b.OffGridEvents.size()) {
				return "off-grid events"_s;
			}
			for (std::size_t i = 0; i < a.OffGridEvents.size(); i++) {
				if (a.OffGridEvents[i].X != b.OffGridEvents[i].X || a.OffGridEvents[i].Y != b.OffGridEvents[i].Y ||
					!EventsEqual(a.OffGridEvents[i].Event, b.OffGridEvents[i].Event)) {
					return "off-grid events"_s;
				}
			}

			if (a.Chunks.size() != b.Chunks.size()) {
				return "chunks"_s;
			}
			for (std::size_t i = 0; i < a.Chunks.size(); i++) {
				// An empty payload may have no buffer at all, which memcmp() must not be given even for zero bytes
				if (a.Chunks[i].Tag != b.Chunks[i].Tag || a.Chunks[i].Data.size() != b.Chunks[i].Data.size() ||
					(!a.Chunks[i].Data.empty() && std::memcmp(a.Chunks[i].Data.data(), b.Chunks[i].Data.data(), a.Chunks[i].Data.size()) != 0)) {
					return "chunks"_s;
				}
			}

			return {};
		}

		/** Offsets the files are cut at to see that the readers refuse them, the header byte by byte and the rest spread out */
		SmallVector<std::int64_t, 0> GetTruncationOffsets(std::int64_t size)
		{
			constexpr std::int64_t HeaderCuts = 32;
			constexpr std::int64_t SpreadCuts = 40;

			SmallVector<std::int64_t, 0> cuts;
			for (std::int64_t i = 0; i < HeaderCuts && i < size; i++) {
				cuts.push_back(i);
			}
			for (std::int64_t i = 1; i <= SpreadCuts; i++) {
				std::int64_t cut = size * i / (SpreadCuts + 1);
				if (cut >= HeaderCuts && cut < size) {
					cuts.push_back(cut);
				}
			}
			for (std::int64_t i = std::max<std::int64_t>(size - 4, HeaderCuts); i < size; i++) {
				cuts.push_back(i);
			}
			return cuts;
		}

		/**
			Checks what the reader can't check on its own, how the records refer to each other and whether the embedded
			tile sets can be read; returns the number of problems found, the first few are logged
		*/
		std::int32_t ValidateLevel(StringView path, const LevelData& level)
		{
			std::int32_t problems = 0;
			auto report = [&](const char* what) {
				if (problems < 4) {
					LOGW("Level \"{}\": {}", path, what);
				}
				problems++;
			};

			if (!LevelFile::IsSafeTileSetPath(level.Properties.DefaultTileSet)) {
				report("The default tile set isn't a path the game would look up");
			}
			for (const ExtraTileSetRecord& tileSet : level.ExtraTileSets) {
				if ((tileSet.Flags & ExtraTileSetRecord::Embedded) != 0) {
					MemoryStream s(tileSet.Data.data(), std::int64_t(tileSet.Data.size()));
					auto data = std::make_unique<TileSetData>();
					if (!TileSetFile::Read(s, *data)) {
						report("An embedded tile set cannot be read");
					} else if (std::int32_t(tileSet.Offset) + std::int32_t(tileSet.Count) > data->TileCount) {
						report("An embedded tile set has fewer tiles than the level takes from it");
					}
				} else if (!LevelFile::IsSafeTileSetPath(tileSet.Name)) {
					report("An extra tile set isn't a path the game would look up");
				}
			}

			const std::int32_t tileIdSpace = std::int32_t(level.AnimatedTilesOffset) + std::int32_t(level.AnimatedTiles.size());
			for (const AnimatedTileRecord& animatedTile : level.AnimatedTiles) {
				bool found = false;
				for (const TileRecord& frame : animatedTile.Frames) {
					found |= (frame.TileId >= tileIdSpace);
				}
				if (found) {
					report("An animated tile shows a tile beyond the tiles of the level");
					break;
				}
			}
			for (const LayerData& layer : level.Layers) {
				for (const TileRecord& tile : layer.Tiles) {
					if (tile.TileId >= tileIdSpace) {
						report("A layer has a tile beyond the tiles of the level");
						break;
					}
				}
			}
			for (const TileDiffuseOverride& tile : level.DiffuseOverrides) {
				if (tile.TileId >= level.AnimatedTilesOffset) {
					report("An overridden tile isn't a tile of a tile set");
					break;
				}
			}
			for (const TileMaskOverride& tile : level.MaskOverrides) {
				if (tile.TileId >= level.AnimatedTilesOffset) {
					report("An overridden tile mask isn't a tile of a tile set");
					break;
				}
			}

			// The game leaves out the off-grid events farther than this from the level (see Events::EventMap)
			for (const LayerData& layer : level.Layers) {
				if (layer.Header.Kind != LayerKind::Sprite) {
					continue;
				}
				const std::uint32_t maxX = std::uint32_t(layer.Header.Width + 64) * LevelFile::TileSize;
				const std::uint32_t maxY = std::uint32_t(layer.Header.Height + 64) * LevelFile::TileSize;
				for (const OffGridEventRecord& event : level.OffGridEvents) {
					if (event.X >= maxX || event.Y >= maxY) {
						report("An off-grid event lies too far outside of the level");
						break;
					}
				}
			}
			return problems;
		}

		/** Reads the level cut short at many places, which must never pass for something it isn't */
		bool SweepTruncatedLevel(StringView path, const SmallVector<std::uint8_t, 0>& original, const LevelData& whole)
		{
			for (std::int64_t cut : GetTruncationOffsets(std::int64_t(original.size()))) {
				auto level = std::make_unique<LevelData>();
				if (ReadLevel(original.data(), cut, *level) == ReadResult::Success) {
					// A cut that took only the end marker of the compressed body can leave all of the content readable
					StringView difference = FindDifference(whole, *level);
					if (!difference.empty()) {
						LOGE("Level \"{}\" cut to {} of {} bytes was read differing in {}", path, cut, original.size(), difference);
						return false;
					}
				}
			}
			return true;
		}

		VerifyResult VerifyLevel(StringView path, bool sweepTruncated, std::int32_t& filesWithProblems)
		{
			auto s = fs::Open(path, FileAccess::Read);
			if (!s->IsValid()) {
				LOGE("Cannot open \"{}\"", path);
				return VerifyResult::Failed;
			}

			std::int64_t fileSize = s->GetSize();
			SmallVector<std::uint8_t, 0> original;
			original.resize_for_overwrite(std::size_t(fileSize));
			if (s->Read(original.data(), fileSize) != fileSize) {
				LOGE("Cannot read \"{}\"", path);
				return VerifyResult::Failed;
			}
			s = nullptr;

			// Everything is read first, the way the editor loads a level
			auto level = std::make_unique<LevelData>();
			switch (ReadLevel(original.data(), std::int64_t(original.size()), *level)) {
				case ReadResult::NotNative: return VerifyResult::NotNative;
				case ReadResult::Failed: LOGE("Level \"{}\" cannot be read: {}", path, level->Error); return VerifyResult::Failed;
				default: break;
			}

			if (ValidateLevel(path, *level) > 0) {
				filesWithProblems++;
			}
			if (sweepTruncated && !SweepTruncatedLevel(path, original, *level)) {
				return VerifyResult::Failed;
			}

			// Then everything is written back, which has to give the same file
			MemoryStream output(std::int64_t(original.size()) + 1024);
			if (!WriteLevel(output, *level)) {
				LOGE("Level \"{}\" cannot be written", path);
				return VerifyResult::Failed;
			}

			if (output.GetSize() == std::int64_t(original.size()) &&
				std::memcmp(output.GetBuffer(), original.data(), original.size()) == 0) {
				return VerifyResult::Identical;
			}

			// A level written by another tool (the C# generator of the test levels, for one) is compressed
			// differently and may store some things the long way, so it's what it holds that has to be the same
			auto written = std::make_unique<LevelData>();
			if (ReadLevel(output.GetBuffer(), output.GetSize(), *written) != ReadResult::Success) {
				LOGE("Level \"{}\" cannot be read after it was written", path);
				return VerifyResult::Failed;
			}

			StringView difference = FindDifference(*level, *written);
			if (difference.empty()) {
				return VerifyResult::Equivalent;
			}

			LOGW("Level \"{}\" differs in {} after being written again", path, difference);
			return VerifyResult::Different;
		}

		/** Returns `true` if two tile sets hold the same tiles, comparing only the bytes of a tile that are used */
		bool TileSetsEqual(const TileSetData& a, const TileSetData& b)
		{
			if (a.TileCount != b.TileCount || std::memcmp(a.Palette, b.Palette, sizeof(a.Palette)) != 0 ||
				a.Masks.size() != b.Masks.size() || std::memcmp(a.Masks.data(), b.Masks.data(), a.Masks.size()) != 0) {
				return false;
			}
			for (std::int32_t i = 0; i < a.TileCount; i++) {
				if ((a.TrueColor[i] != 0) != (b.TrueColor[i] != 0)) {
					return false;
				}
				const std::size_t bytes = (a.TrueColor[i] != 0 ? TileSetData::ImageBytesPerTile : TileSetFile::TileSize * TileSetFile::TileSize);
				if (std::memcmp(&a.Images[std::size_t(i) * TileSetData::ImageBytesPerTile], &b.Images[std::size_t(i) * TileSetData::ImageBytesPerTile], bytes) != 0) {
					return false;
				}
			}
			return true;
		}

		VerifyResult VerifyTileSet(StringView path, bool sweepTruncated)
		{
			auto s = fs::Open(path, FileAccess::Read);
			if (!s->IsValid()) {
				LOGE("Cannot open \"{}\"", path);
				return VerifyResult::Failed;
			}

			std::int64_t fileSize = s->GetSize();
			SmallVector<std::uint8_t, 0> original;
			original.resize_for_overwrite(std::size_t(fileSize));
			if (s->Read(original.data(), fileSize) != fileSize) {
				LOGE("Cannot read \"{}\"", path);
				return VerifyResult::Failed;
			}
			s = nullptr;

			// The signature tells a tile set of the game's own format, a header that doesn't pass the validation is a damaged one
			TileSetHeader header;
			{
				MemoryStream input(original.data(), std::int64_t(original.size()));
				std::uint64_t signature = input.ReadValueAsLE<std::uint64_t>();
				std::uint16_t signature2 = input.ReadValueAsLE<std::uint16_t>();
				std::uint8_t version = input.ReadValue<std::uint8_t>();
				if (original.size() < 11 || signature != TileSetFile::Signature || signature2 != TileSetFile::Signature2 ||
					version != TileSetFile::CurrentVersion) {
					return VerifyResult::NotNative;
				}
				input.Seek(0, SeekOrigin::Begin);
				if (!TileSetFile::ReadHeader(input, header)) {
					LOGE("Tile set \"{}\" has an invalid header", path);
					return VerifyResult::Failed;
				}
			}

			auto tileSet = std::make_unique<TileSetData>();
			{
				MemoryStream input(original.data(), std::int64_t(original.size()));
				if (!TileSetFile::Read(input, *tileSet)) {
					LOGE("Tile set \"{}\" cannot be read", path);
					return VerifyResult::Failed;
				}
			}

			// Cut short anywhere before the image content it must be refused, the image content has no length of its own,
			// so a cut there only leaves the rest of the pixels empty
			if (sweepTruncated) {
				constexpr std::int64_t HeaderSize = 8 + 2 + 1 + 1 + 1 + 4 + 4 + 2 + 4;
				const std::int64_t imageStart = HeaderSize + header.BodySize;
				for (std::int64_t cut : GetTruncationOffsets(std::int64_t(original.size()))) {
					MemoryStream input(original.data(), cut);
					auto truncated = std::make_unique<TileSetData>();
					if (TileSetFile::Read(input, *truncated) && cut < imageStart) {
						LOGE("Tile set \"{}\" cut to {} of {} bytes was read", path, cut, original.size());
						return VerifyResult::Failed;
					}
				}
			}

			MemoryStream output(std::int64_t(original.size()) + 1024);
			if (!TileSetFile::Write(output, *tileSet, (header.Flags & TileSetFile::ImageLz4) != 0)) {
				LOGE("Tile set \"{}\" cannot be written", path);
				return VerifyResult::Failed;
			}

			if (output.GetSize() == std::int64_t(original.size()) &&
				std::memcmp(output.GetBuffer(), original.data(), original.size()) == 0) {
				return VerifyResult::Identical;
			}

			auto written = std::make_unique<TileSetData>();
			output.Seek(0, SeekOrigin::Begin);
			if (!TileSetFile::Read(output, *written)) {
				LOGE("Tile set \"{}\" cannot be read after it was written", path);
				return VerifyResult::Failed;
			}
			if (TileSetsEqual(*tileSet, *written)) {
				return VerifyResult::Equivalent;
			}

			LOGW("Tile set \"{}\" differs after being written again", path);
			return VerifyResult::Different;
		}

		void CollectFiles(StringView path, StringView extension, SmallVectorImpl<String>& files)
		{
			for (auto item : fs::Directory(path, fs::EnumerationOptions::SkipDirectories)) {
				if (fs::GetExtension(item) == extension) {
					files.push_back(item);
				}
			}
			for (auto item : fs::Directory(path, fs::EnumerationOptions::SkipFiles)) {
				CollectFiles(item, extension, files);
			}
		}

		struct VerifyCounts {
			std::int32_t Identical = 0, Equivalent = 0, Different = 0, NotNative = 0, Failed = 0;

			void Add(VerifyResult result) {
				switch (result) {
					case VerifyResult::Identical: Identical++; break;
					case VerifyResult::Equivalent: Equivalent++; break;
					case VerifyResult::Different: Different++; break;
					case VerifyResult::NotNative: NotNative++; break;
					default: Failed++; break;
				}
			}
		};
	}

	bool FormatVerifier::VerifyDirectory(StringView path, bool sweepTruncated)
	{
		if (!fs::DirectoryExists(path)) {
			LOGE("Directory \"{}\" doesn't exist", path);
			return false;
		}

		SmallVector<String, 0> levels;
		CollectFiles(path, "j2l"_s, levels);
		VerifyCounts levelCounts;
		std::int32_t levelsWithProblems = 0;
		for (const String& level : levels) {
			levelCounts.Add(VerifyLevel(level, sweepTruncated, levelsWithProblems));
		}

		SmallVector<String, 0> tileSets;
		CollectFiles(path, "j2t"_s, tileSets);
		VerifyCounts tileSetCounts;
		for (const String& tileSet : tileSets) {
			tileSetCounts.Add(VerifyTileSet(tileSet, sweepTruncated));
		}

		LOGI("{} level(s) verified: {} identical, {} with the same content written differently, {} different, {} failed, {} not in the native format",
			levelCounts.Identical + levelCounts.Equivalent + levelCounts.Different + levelCounts.Failed, levelCounts.Identical,
			levelCounts.Equivalent, levelCounts.Different, levelCounts.Failed, levelCounts.NotNative);
		if (levelsWithProblems > 0) {
			// Readable, but referring to things that aren't there; the game copes with them, so they don't fail the check
			LOGW("{} level(s) have references the game ignores, see the warnings above", levelsWithProblems);
		}
		LOGI("{} tile set(s) verified: {} identical, {} with the same content written differently, {} different, {} failed, {} not in the native format",
			tileSetCounts.Identical + tileSetCounts.Equivalent + tileSetCounts.Different + tileSetCounts.Failed, tileSetCounts.Identical,
			tileSetCounts.Equivalent, tileSetCounts.Different, tileSetCounts.Failed, tileSetCounts.NotNative);
		return (levelCounts.Different == 0 && levelCounts.Failed == 0 && tileSetCounts.Different == 0 && tileSetCounts.Failed == 0);
	}

	namespace
	{
		using Bytes = SmallVector<std::uint8_t, 0>;

		constexpr std::size_t LevelHeaderSize = 8 + 1 + 2 + 2 + 4;
		constexpr std::size_t TileSetHeaderSize = 8 + 2 + 1 + 1 + 1 + 4 + 4 + 2 + 4;

		/** splitmix64, small and the same everywhere, so a seed always gives the same inputs */
		class FuzzRandom
		{
		public:
			explicit FuzzRandom(std::uint64_t seed) : _state(seed) {}

			std::uint64_t Next() {
				std::uint64_t z = (_state += 0x9E3779B97F4A7C15ull);
				z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
				z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
				return z ^ (z >> 31);
			}

			std::uint32_t Below(std::uint32_t count) {
				return (count > 0 ? std::uint32_t(Next() % count) : 0);
			}

			std::size_t Position(std::size_t start, std::size_t end) {
				return (end > start ? start + std::size_t(Next() % (end - start)) : start);
			}

			/** A value that tends to be on an edge of something, a size, a count or a sign */
			std::uint32_t Interesting() {
				static const std::uint32_t Values[] = {
					0, 1, 2, 3, 0x7F, 0x80, 0xFF, 0x100, 0x7FFF, 0x8000, 0xFFFF, 0x10000, 0x10001, 32767, 32768,
					0x7FFFFFFF, 0x80000000, 0xFFFFFFFE, 0xFFFFFFFF, LevelFile::MaxBlobSize, LevelFile::MaxBlobSize + 1,
					std::uint32_t(LevelFile::MaxLayerTiles), LevelFile::MaxOffGridEvents + 1, TileSetFile::MaxImageWidth + 32
				};
				return (Below(4) == 0 ? std::uint32_t(Next()) : Values[Below(std::uint32_t(sizeof(Values) / sizeof(Values[0])))]);
			}

		private:
			std::uint64_t _state;
		};

		/** A file the inputs are made of, in the form the mutations start from */
		struct FuzzSample {
			String Path;
			String Name;
			bool IsLevel = false;
			/** The file, a level as the writer writes it again, so the offsets below are exact */
			Bytes File;
			/** The decompressed body of a level */
			Bytes Body;
			/** Where the chunk list starts in the body of a level, the body ends there if it has none */
			std::size_t ChunksStart = 0;
			/** The records of a level, for the mutations of the records */
			std::unique_ptr<LevelData> Level;
		};

		void AppendVariableUint32(Bytes& data, std::uint32_t value)
		{
			do {
				std::uint8_t byte = std::uint8_t(value & 0x7F);
				value >>= 7;
				if (value != 0) {
					byte |= 0x80;
				}
				data.push_back(byte);
			} while (value != 0);
		}

		void AppendUint32(Bytes& data, std::uint32_t value)
		{
			for (std::int32_t i = 0; i < 4; i++) {
				data.push_back(std::uint8_t(value >> (i * 8)));
			}
		}

		void AppendStream(Bytes& data, MemoryStream& s)
		{
			const std::uint8_t* buffer = reinterpret_cast<const std::uint8_t*>(s.GetBuffer());
			data.append(buffer, buffer + s.GetSize());
		}

		bool InflateRaw(const std::uint8_t* data, std::size_t size, Bytes& output)
		{
			MemoryStream input(data, std::int64_t(size));
			DeflateStream uc(input, std::int32_t(size));
			output.clear();
			std::uint8_t buffer[16384];
			while (true) {
				std::int64_t bytesRead = uc.Read(buffer, sizeof(buffer));
				if (bytesRead <= 0) {
					return (bytesRead == 0);
				}
				output.append(buffer, buffer + bytesRead);
				if (output.size() > std::size_t(LevelFile::MaxBodySize)) {
					return false;
				}
			}
		}

		void DeflateRaw(const Bytes& data, Bytes& output)
		{
			MemoryStream ms(std::int64_t(data.size() / 2 + 1024));
			{
				DeflateWriter co(ms, 1);
				co.Write(data.data(), std::int64_t(data.size()));
			}
			output.clear();
			AppendStream(output, ms);
		}

		/** Changes the data a little in one of many ways, mostly within the given range */
		void MutateBytes(FuzzRandom& random, Bytes& data, std::size_t focusStart, std::size_t focusEnd)
		{
			if (focusEnd > data.size()) {
				focusEnd = data.size();
			}
			if (focusStart >= focusEnd) {
				focusStart = 0;
				focusEnd = data.size();
			}
			const std::size_t pos = random.Position(focusStart, focusEnd);
			std::uint32_t op = (data.empty() ? 3 : random.Below(9));
			switch (op) {
				case 0: {
					// A few bits flipped
					std::uint32_t count = 1 + random.Below(8);
					for (std::uint32_t i = 0; i < count; i++) {
						data[random.Position(focusStart, focusEnd)] ^= std::uint8_t(1u << random.Below(8));
					}
					break;
				}
				case 1: {
					// A run of the same or of random bytes
					std::size_t length = std::min<std::size_t>(1 + random.Below(64), data.size() - pos);
					std::uint32_t kind = random.Below(4);
					std::uint8_t value = std::uint8_t(kind == 0 ? 0x00 : (kind == 1 ? 0xFF : random.Next()));
					for (std::size_t i = 0; i < length; i++) {
						data[pos + i] = (kind == 3 ? std::uint8_t(random.Next()) : value);
					}
					break;
				}
				case 2: {
					// A number of 1, 2 or 4 bytes set to a value on an edge
					std::uint32_t value = random.Interesting();
					std::size_t width = std::min<std::size_t>(std::size_t(1) << random.Below(3), data.size() - pos);
					for (std::size_t i = 0; i < width; i++) {
						data[pos + i] = std::uint8_t(value >> (i * 8));
					}
					break;
				}
				case 3: {
					// Bytes inserted
					std::size_t length = 1 + random.Below(64);
					std::uint32_t kind = random.Below(3);
					std::uint8_t value = std::uint8_t(kind == 0 ? 0x00 : 0xFF);
					Bytes inserted;
					for (std::size_t i = 0; i < length; i++) {
						inserted.push_back(kind == 2 ? std::uint8_t(random.Next()) : value);
					}
					data.insert(data.begin() + std::min(pos, data.size()), inserted.begin(), inserted.end());
					break;
				}
				case 4: {
					// Bytes removed
					std::size_t length = std::min<std::size_t>(1 + random.Below(256), data.size() - pos);
					data.erase(data.begin() + pos, data.begin() + pos + length);
					break;
				}
				case 5: {
					// A part repeated somewhere else
					std::size_t from = random.Position(0, data.size());
					std::size_t length = std::min<std::size_t>(1 + random.Below(1024), data.size() - from);
					Bytes copy;
					copy.append(data.begin() + from, data.begin() + from + length);
					data.insert(data.begin() + pos, copy.begin(), copy.end());
					break;
				}
				case 6: {
					// A number stored the long way, or as large as it gets
					Bytes inserted;
					if (random.Below(2) == 0) {
						data[pos] |= 0x80;
						std::uint32_t extra = 1 + random.Below(8);
						for (std::uint32_t i = 0; i < extra; i++) {
							inserted.push_back(0x80);
						}
						inserted.push_back(std::uint8_t(random.Below(2) == 0 ? 0x00 : 0x0F));
					} else {
						inserted.push_back(0xFF);
						inserted.push_back(0xFF);
						inserted.push_back(0xFF);
						inserted.push_back(0xFF);
						inserted.push_back(std::uint8_t(random.Below(2) == 0 ? 0x0F : 0x7F));
					}
					data.insert(data.begin() + pos + 1, inserted.begin(), inserted.end());
					break;
				}
				case 7: {
					// Cut short
					data.erase(data.begin() + pos, data.end());
					break;
				}
				default: {
					// A byte copied from elsewhere
					data[pos] = data[random.Position(0, data.size())];
					break;
				}
			}
		}

		void MutateBytesMany(FuzzRandom& random, Bytes& data, std::size_t focusStart, std::size_t focusEnd)
		{
			std::uint32_t count = 1 + random.Below(4);
			for (std::uint32_t i = 0; i < count; i++) {
				MutateBytes(random, data, focusStart, focusEnd);
			}
		}

		/** A mutated copy of a tile set file, of its header, of its decompressed block or of its image content */
		void MutateTileSet(FuzzRandom& random, const Bytes& file, Bytes& output)
		{
			output.clear();
			output.append(file.begin(), file.end());

			std::int64_t bodySize = -1;
			if (file.size() >= TileSetHeaderSize) {
				bodySize = std::int32_t(std::uint32_t(file[23]) | (std::uint32_t(file[24]) << 8) | (std::uint32_t(file[25]) << 16) | (std::uint32_t(file[26]) << 24));
			}
			const bool hasBlock = (bodySize > 0 && TileSetHeaderSize + std::size_t(bodySize) <= file.size());

			switch (random.Below(8)) {
				case 0:
				case 1:
					MutateBytesMany(random, output, 0, random.Below(2) == 0 ? TileSetHeaderSize : output.size());
					break;
				case 2: {
					// The fields of the header: flags, channels, width, height, tile count and the size of the block
					static const std::uint8_t Offsets[] = { 11, 12, 13, 17, 21, 23 };
					static const std::uint8_t Widths[] = { 1, 1, 4, 4, 2, 4 };
					std::uint32_t count = 1 + random.Below(2);
					for (std::uint32_t i = 0; i < count && output.size() >= TileSetHeaderSize; i++) {
						std::uint32_t field = random.Below(6);
						std::uint32_t value = random.Interesting();
						for (std::uint32_t j = 0; j < Widths[field]; j++) {
							output[Offsets[field] + j] = std::uint8_t(value >> (j * 8));
						}
					}
					break;
				}
				case 3:
				case 4:
				case 5: {
					// The palette, the true color bits and the masks, attacked decompressed
					Bytes block;
					if (!hasBlock || !InflateRaw(file.data() + TileSetHeaderSize, std::size_t(bodySize), block)) {
						MutateBytesMany(random, output, 0, output.size());
						break;
					}
					MutateBytesMany(random, block, 0, block.size());
					Bytes compressed;
					DeflateRaw(block, compressed);
					output.clear();
					output.append(file.begin(), file.begin() + 23);
					AppendUint32(output, random.Below(16) == 0 ? random.Interesting() : std::uint32_t(compressed.size()));
					output.append(compressed.begin(), compressed.end());
					output.append(file.begin() + TileSetHeaderSize + std::size_t(bodySize), file.end());
					break;
				}
				default:
					// The image content
					MutateBytesMany(random, output, hasBlock ? TileSetHeaderSize + std::size_t(bodySize) : 0, output.size());
					break;
			}
		}

		/** A file of a level put together of the header of a sample and a body, compressed */
		void AssembleLevel(FuzzRandom& random, const FuzzSample& sample, const Bytes& body, Bytes& output)
		{
			Bytes compressed;
			DeflateRaw(body, compressed);
			output.clear();
			output.append(sample.File.begin(), sample.File.begin() + (LevelHeaderSize - 4));
			AppendUint32(output, random.Below(16) == 0 ? random.Interesting() : std::uint32_t(compressed.size()));
			output.append(compressed.begin(), compressed.end());
		}

		/** A chunk list made up, with any counts, sizes and payloads that are shorter than they claim */
		void AppendCraftedChunks(FuzzRandom& random, Bytes& body)
		{
			const std::uint32_t Tags[] = {
				LevelFile::PreviewChunk, LayerPropertiesChunk::Tag, ScriptDataChunk::Tag, LevelTextsChunk::Tag, WeaponsChunk::Tag,
				LevelFile::MakeChunkTag('e', 'd', 'o', 'c'),
				LevelFile::MakeChunkTag('a', 'n', 'n', 'o'), LevelFile::MakeChunkTag('s', 'p', 'l', 'n'),
				LevelFile::MakeChunkTag('e', 'l', 'a', 'y'), LevelFile::MakeChunkTag('r', 'c', 'o', 'v'),
				LevelFile::MakeChunkTag('G', 's', 'c', 'r'), LevelFile::MakeChunkTag('X', 'Y', 'Z', 'W')
			};
			const std::uint32_t count = (random.Below(4) == 0 ? random.Interesting() : random.Below(6));
			AppendVariableUint32(body, count);
			const std::uint32_t written = std::min<std::uint32_t>(count, 6);
			for (std::uint32_t i = 0; i < written; i++) {
				std::uint32_t tagIndex = random.Below(std::uint32_t(sizeof(Tags) / sizeof(Tags[0])) + 1);
				AppendUint32(body, tagIndex < sizeof(Tags) / sizeof(Tags[0]) ? Tags[tagIndex] : std::uint32_t(random.Next()));
				std::uint32_t size = (random.Below(3) == 0 ? random.Interesting() : random.Below(300));
				AppendVariableUint32(body, size);
				std::uint32_t payload = std::min<std::uint32_t>(size, 4096);
				if (random.Below(4) == 0) {
					payload = random.Below(payload + 1);
				}
				for (std::uint32_t j = 0; j < payload; j++) {
					body.push_back(std::uint8_t(random.Next()));
				}
			}
		}

		/** Random translations of the level texts, with odd languages, numbers and bytes that aren't UTF-8 */
		void MakeRandomLevelTexts(FuzzRandom& random, SmallVectorImpl<LocalizedLevelTexts>& languages)
		{
			static const StringView Names[] = { "cs"_s, "de"_s, "pt-br"_s, "zh_CN"_s, "en"_s, ""_s };
			bool hasLongText = false;
			std::uint32_t count = 1 + random.Below(6);
			for (std::uint32_t i = 0; i < count; i++) {
				LocalizedLevelTexts& language = languages.emplace_back();
				if (random.Below(5) == 0) {
					language.Language = String(NoInit, random.Below(300));
					for (char& ch : language.Language) {
						ch = char(random.Next());
					}
				} else {
					language.Language = Names[random.Below(std::uint32_t(sizeof(Names) / sizeof(Names[0])))];
				}
				std::uint32_t texts = (random.Below(4) == 0 ? LevelTextsChunk::MaxTexts + random.Below(3) : random.Below(20));
				for (std::uint32_t j = 0; j < texts; j++) {
					String& text = language.Texts.emplace_back();
					if (random.Below(2) == 0) {
						continue;
					}
					// Rarely one longer than the chunk keeps, cut by the writer
					std::uint32_t length = (!hasLongText && random.Below(64) == 0 ? LevelTextsChunk::MaxTextLength + 1 + random.Below(16) : random.Below(64));
					hasLongText |= (length > LevelTextsChunk::MaxTextLength);
					text = String(NoInit, length);
					for (char& ch : text) {
						ch = char(random.Below(4) == 0 ? random.Next() : 'a' + random.Below(26));
					}
				}
			}
		}

		/** Gives the level a chunk of translated texts made by the writer of the chunk, sometimes damaged */
		void SetRandomLevelTexts(FuzzRandom& random, LevelData& level, bool damage)
		{
			SmallVector<LocalizedLevelTexts, 0> languages;
			MakeRandomLevelTexts(random, languages);
			Bytes data;
			LevelTextsChunk::Write(languages, data);
			if (damage) {
				MutateBytesMany(random, data, 0, data.size());
			}
			for (std::size_t i = 0; i < level.Chunks.size(); i++) {
				if (level.Chunks[i].Tag == LevelTextsChunk::Tag) {
					level.Chunks.erase(level.Chunks.begin() + i);
					break;
				}
			}
			ChunkRecord& chunk = level.Chunks.emplace_back();
			chunk.Tag = LevelTextsChunk::Tag;
			chunk.Data.append(data.begin(), data.end());
		}

		/** Compares translated texts the way the writer of the chunk keeps them, languages with a text, texts by number */
		bool LevelTextsEqual(ArrayView<const LocalizedLevelTexts> a, ArrayView<const LocalizedLevelTexts> b)
		{
			auto collectKept = [](ArrayView<const LocalizedLevelTexts> list, SmallVectorImpl<const LocalizedLevelTexts*>& kept) {
				for (const LocalizedLevelTexts& language : list) {
					// Texts past the last number the chunk can hold are never written
					bool hasText = false;
					for (std::size_t j = 0; j < language.Texts.size() && j < LevelTextsChunk::MaxTexts; j++) {
						hasText |= !language.Texts[j].empty();
					}
					if (!language.Language.empty() && hasText && kept.size() < LevelTextsChunk::MaxLanguages) {
						kept.push_back(&language);
					}
				}
			};
			SmallVector<const LocalizedLevelTexts*, 0> keptA, keptB;
			collectKept(a, keptA);
			collectKept(b, keptB);
			if (keptA.size() != keptB.size()) {
				return false;
			}
			for (std::size_t i = 0; i < keptA.size(); i++) {
				const LocalizedLevelTexts& la = *keptA[i];
				const LocalizedLevelTexts& lb = *keptB[i];
				if (la.Language.prefix(std::min<std::size_t>(la.Language.size(), UINT8_MAX)) != lb.Language) {
					return false;
				}
				const std::size_t textCount = std::min<std::size_t>(std::max(la.Texts.size(), lb.Texts.size()), LevelTextsChunk::MaxTexts);
				for (std::size_t j = 0; j < textCount; j++) {
					StringView ta = (j < la.Texts.size() ? StringView(la.Texts[j]) : StringView());
					StringView tb = (j < lb.Texts.size() ? StringView(lb.Texts[j]) : StringView());
					if (ta.prefix(Death::Utf8::FitPrefix(ta, LevelTextsChunk::MaxTextLength)) != tb) {
						return false;
					}
				}
			}
			return true;
		}

		/** A level with its records changed and written by the writer, so the content is consistent but anything */
		void MakeLevelOfRecords(FuzzRandom& random, const FuzzSample& sample, ArrayView<const FuzzSample*> tileSets, bool focusLevelTexts, Bytes& output)
		{
			auto level = std::make_unique<LevelData>(*sample.Level);
			std::uint32_t count = 1 + random.Below(3);
			for (std::uint32_t c = 0; c < count; c++) {
				std::uint32_t kind = random.Below(11);
				if (focusLevelTexts && random.Below(2) == 0) {
					kind = 10;
				}
				switch (kind) {
					case 10:
						SetRandomLevelTexts(random, *level, random.Below(3) == 0);
						break;
					case 0:
						level->AnimatedTilesOffset = std::uint16_t(random.Interesting());
						break;
					case 1: {
						if (level->AnimatedTiles.empty() || random.Below(4) == 0) {
							level->AnimatedTiles.emplace_back();
						}
						AnimatedTileRecord& animatedTile = level->AnimatedTiles[random.Below(std::uint32_t(level->AnimatedTiles.size()))];
						animatedTile.FrameDuration16 = std::uint16_t(random.Interesting());
						animatedTile.Delay = std::uint16_t(random.Interesting());
						animatedTile.DelayJitter = std::uint16_t(random.Interesting());
						animatedTile.PingPongDelay = std::uint16_t(random.Interesting());
						animatedTile.IsPingPong = std::uint8_t(random.Next());
						std::uint32_t frames = random.Below(16);
						for (std::uint32_t i = 0; i < frames && animatedTile.Frames.size() < 255; i++) {
							TileRecord& frame = animatedTile.Frames.emplace_back();
							frame.Flags = std::uint8_t(random.Next());
							frame.TileId = std::uint16_t(random.Interesting());
						}
						break;
					}
					case 2: {
						LayerData& layer = level->Layers[random.Below(std::uint32_t(level->Layers.size()))];
						std::uint32_t tiles = 1 + random.Below(64);
						for (std::uint32_t i = 0; i < tiles && !layer.Tiles.empty(); i++) {
							TileRecord& tile = layer.Tiles[random.Position(0, layer.Tiles.size())];
							tile.Flags = std::uint8_t(random.Next());
							tile.TileId = std::uint16_t(random.Interesting());
						}
						break;
					}
					case 3: {
						LayerData& layer = level->Layers[random.Below(std::uint32_t(level->Layers.size()))];
						static const float Floats[] = { 0.0f, -0.0f, 1.0f, -1.0f, 1e30f, -1e30f, 3.4e38f, 1e-40f, 65536.0f };
						float* fields[] = { &layer.Header.OffsetX, &layer.Header.OffsetY, &layer.Header.SpeedX, &layer.Header.SpeedY,
							&layer.Header.AutoSpeedX, &layer.Header.AutoSpeedY };
						*fields[random.Below(6)] = Floats[random.Below(std::uint32_t(sizeof(Floats) / sizeof(Floats[0])))];
						layer.Header.SpeedModels = std::uint8_t(random.Next());
						layer.Header.RendererType = std::uint8_t(random.Next());
						layer.Header.Depth = std::int16_t(random.Interesting());
						layer.Header.Flags = std::uint16_t(random.Interesting());
						break;
					}
					case 4: {
						std::uint32_t events = 1 + random.Below(32);
						for (std::uint32_t i = 0; i < events && !level->Events.empty(); i++) {
							EventRecord& event = level->Events[random.Position(0, level->Events.size())];
							event.Type = std::uint16_t(random.Below(4) == 0 ? random.Interesting() : random.Below(200));
							event.Flags = std::uint8_t(random.Next());
							event.GeneratorFlags = std::uint8_t(random.Next());
							event.GeneratorDelay = std::uint8_t(random.Next());
							for (std::int32_t j = 0; j < LevelFile::EventParamsSize; j++) {
								event.Params[j] = std::uint8_t(random.Below(2) == 0 ? 0 : random.Next());
							}
						}
						break;
					}
					case 5: {
						std::uint32_t events = 1 + random.Below(48);
						for (std::uint32_t i = 0; i < events; i++) {
							OffGridEventRecord& event = level->OffGridEvents.emplace_back();
							event.X = (random.Below(2) == 0 ? random.Interesting() : random.Below(100000));
							event.Y = (random.Below(2) == 0 ? random.Interesting() : random.Below(100000));
							std::memset(&event.Event, 0, sizeof(event.Event));
							event.Event.Type = std::uint16_t(random.Below(200));
							event.Event.Flags = std::uint8_t(random.Next());
							event.Event.Params[0] = std::uint8_t(random.Next());
						}
						break;
					}
					case 6: {
						if (random.Below(2) == 0) {
							TileDiffuseOverride& tile = level->DiffuseOverrides.emplace_back();
							tile.TileId = std::uint16_t(random.Interesting());
							std::memset(tile.Indices, std::uint8_t(random.Next()), sizeof(tile.Indices));
						} else {
							TileMaskOverride& tile = level->MaskOverrides.emplace_back();
							tile.TileId = std::uint16_t(random.Interesting());
							std::memset(tile.Mask, std::uint8_t(random.Next()), sizeof(tile.Mask));
						}
						break;
					}
					case 7: {
						ExtraTileSetRecord& tileSet = (level->ExtraTileSets.empty() || random.Below(3) == 0
							? level->ExtraTileSets.emplace_back() : level->ExtraTileSets[random.Below(std::uint32_t(level->ExtraTileSets.size()))]);
						static const StringView Names[] = { "castle1"_s, "../../secret"_s, "C:\\Windows\\x"_s, "/etc/x"_s, ":level"_s, ""_s };
						tileSet.Name = Names[random.Below(std::uint32_t(sizeof(Names) / sizeof(Names[0])))];
						tileSet.Flags = std::uint8_t(random.Next() & ~ExtraTileSetRecord::Embedded);
						tileSet.Offset = std::uint16_t(random.Interesting());
						tileSet.Count = std::uint16_t(random.Interesting());
						tileSet.Data.clear();
						break;
					}
					case 8: {
						// An embedded tile set made of a mutated tile set file
						if (tileSets.empty()) {
							break;
						}
						ExtraTileSetRecord& tileSet = level->ExtraTileSets.emplace_back();
						tileSet.Name = ":level"_s;
						tileSet.Flags = std::uint8_t((random.Next() & (ExtraTileSetRecord::Remapped | ExtraTileSetRecord::TrueColor)) | ExtraTileSetRecord::Embedded);
						tileSet.Offset = std::uint16_t(random.Below(4) == 0 ? random.Interesting() : 0);
						tileSet.Count = std::uint16_t(random.Below(4) == 0 ? random.Interesting() : random.Below(256));
						for (std::int32_t i = 0; i < LevelFile::ColorsPerPalette; i++) {
							tileSet.Remapping[i] = std::uint8_t(random.Next());
						}
						tileSet.AlternatePalette = std::uint8_t(random.Next());
						Bytes data;
						MutateTileSet(random, tileSets[random.Below(std::uint32_t(tileSets.size()))]->File, data);
						tileSet.Data.append(data.begin(), data.end());
						break;
					}
					default: {
						// The texts, the strings of the properties and the palette flag with its palette
						String* strings[] = { &level->Properties.DisplayName, &level->Properties.Author, &level->Properties.NextLevel,
							&level->Properties.DefaultTileSet, &level->Properties.Music };
						String& text = *strings[random.Below(5)];
						text = String(NoInit, random.Below(256));
						for (char& ch : text) {
							ch = char(random.Next());
						}
						if (random.Below(2) == 0 && level->Texts.size() < 255) {
							String& levelText = level->Texts.emplace_back(NoInit, random.Below(1024));
							for (char& ch : levelText) {
								ch = char(random.Next());
							}
						}
						if (random.Below(4) == 0) {
							level->HasLevelPalette = !level->HasLevelPalette;
							level->Header.Flags = (level->HasLevelPalette ? (level->Header.Flags | LevelFlags::UseLevelPalette) : (level->Header.Flags & ~LevelFlags::UseLevelPalette));
						}
						break;
					}
				}
			}

			MemoryStream s(std::int64_t(sample.File.size()) + 4096);
			WriteLevel(s, *level);
			output.clear();
			AppendStream(output, s);
		}

		void MakeLevelInput(FuzzRandom& random, const FuzzSample& sample, ArrayView<const FuzzSample*> tileSets, bool focusLevelTexts, Bytes& output)
		{
			// With the focus on the translated texts, the chunks and the records (which make the chunk) are attacked more
			std::uint32_t kind = random.Below(10);
			if (focusLevelTexts && kind < 4) {
				kind = (kind < 2 ? 6 : 8);
			}
			if (kind < 2) {
				// The file itself, the header included
				output.clear();
				output.append(sample.File.begin(), sample.File.end());
				MutateBytesMany(random, output, 0, random.Below(3) == 0 ? LevelHeaderSize : output.size());
			} else if (kind < 7) {
				// The decompressed body, compressed again, so the readers are attacked rather than the compression
				Bytes body;
				body.append(sample.Body.begin(), sample.Body.end());
				if (kind == 6) {
					MutateBytesMany(random, body, sample.ChunksStart, body.size());
				} else {
					MutateBytesMany(random, body, 0, body.size());
				}
				AssembleLevel(random, sample, body, output);
			} else if (kind < 8) {
				// A chunk list made up
				Bytes body;
				body.append(sample.Body.begin(), sample.Body.begin() + sample.ChunksStart);
				AppendCraftedChunks(random, body);
				AssembleLevel(random, sample, body, output);
			} else {
				MakeLevelOfRecords(random, sample, tileSets, focusLevelTexts, output);
			}
		}

		struct FuzzOutcome {
			bool Accepted = false;
			/** What differs when what was accepted is written and read again, empty if nothing */
			StringView RoundTripDifference;
		};

		FuzzOutcome RunTileSetReaders(const std::uint8_t* data, std::size_t size)
		{
			FuzzOutcome outcome;
			{
				MemoryStream input(data, std::int64_t(size));
				TileSetHeader header;
				TileSetFile::ReadHeader(input, header);
			}

			MemoryStream input(data, std::int64_t(size));
			auto tileSet = std::make_unique<TileSetData>();
			if (TileSetFile::Read(input, *tileSet)) {
				outcome.Accepted = true;
				MemoryStream output(std::int64_t(size) + 1024);
				if (!TileSetFile::Write(output, *tileSet, false)) {
					outcome.RoundTripDifference = "writing"_s;
				} else {
					output.Seek(0, SeekOrigin::Begin);
					auto written = std::make_unique<TileSetData>();
					if (!TileSetFile::Read(output, *written)) {
						outcome.RoundTripDifference = "reading again"_s;
					} else if (!TileSetsEqual(*tileSet, *written)) {
						outcome.RoundTripDifference = "tiles"_s;
					}
				}
			}
			return outcome;
		}

		FuzzOutcome RunLevelReaders(const std::uint8_t* data, std::size_t size)
		{
			FuzzOutcome outcome;
			{
				// Everything, the way the editor reads it, and what it accepts has to survive being written again
				auto level = std::make_unique<LevelData>();
				if (ReadLevel(data, std::int64_t(size), *level) == ReadResult::Success) {
					outcome.Accepted = true;
					for (const ExtraTileSetRecord& tileSet : level->ExtraTileSets) {
						if ((tileSet.Flags & ExtraTileSetRecord::Embedded) != 0) {
							// The level owns these tiles, so the tile set has to survive the same as the rest of it
							FuzzOutcome tileSetOutcome = RunTileSetReaders(tileSet.Data.data(), tileSet.Data.size());
							if (!tileSetOutcome.RoundTripDifference.empty()) {
								outcome.RoundTripDifference = "embedded tile set"_s;
							}
						}
					}

					// The translated texts, whose chunk has to survive being written again by its own writer too
					for (const ChunkRecord& chunk : level->Chunks) {
						if (chunk.Tag != LevelTextsChunk::Tag) {
							continue;
						}
						SmallVector<LocalizedLevelTexts, 0> languages;
						if (LevelTextsChunk::Read(chunk.Data, languages)) {
							Bytes written;
							LevelTextsChunk::Write(languages, written);
							SmallVector<LocalizedLevelTexts, 0> languagesAgain;
							if (!written.empty() && !LevelTextsChunk::Read(written, languagesAgain)) {
								outcome.RoundTripDifference = "translated texts reading again"_s;
							} else if (!LevelTextsEqual(languages, languagesAgain)) {
								outcome.RoundTripDifference = "translated texts"_s;
							}
						}
					}

					// The properties of the layers, the same - the writer drops the entries of the defaults
					for (const ChunkRecord& chunk : level->Chunks) {
						if (chunk.Tag != LayerPropertiesChunk::Tag) {
							continue;
						}
						SmallVector<LayerPropertiesChunk::Entry, 0> layers;
						if (LayerPropertiesChunk::Read(chunk.Data, layers)) {
							Bytes written;
							LayerPropertiesChunk::Write(layers, written);
							SmallVector<LayerPropertiesChunk::Entry, 0> layersAgain;
							if (!written.empty() && !LayerPropertiesChunk::Read(written, layersAgain)) {
								outcome.RoundTripDifference = "layer properties reading again"_s;
							} else {
								std::size_t next = 0;
								for (const LayerPropertiesChunk::Entry& entry : layers) {
									if (entry.IsDefault()) {
										continue;
									}
									const LayerPropertiesChunk::Entry* again = (next < layersAgain.size() ? &layersAgain[next++] : nullptr);
									const TexturedLayerParams& p1 = entry.Textured;
									if (again == nullptr || again->Layer != entry.Layer || again->Name != entry.Name || again->OriginalNumber != entry.OriginalNumber ||
										again->HasSpriteLook != entry.HasSpriteLook || again->RendererType != entry.RendererType ||
										std::memcmp(again->Color, entry.Color, sizeof(entry.Color)) != 0 || again->Blend != entry.Blend ||
										(entry.Blend != 0 && again->BlendParam != entry.BlendParam) ||
										again->Textured.FadeX != p1.FadeX || again->Textured.FadeY != p1.FadeY || again->Textured.Fade != p1.Fade ||
										again->Textured.Flags != p1.Flags || again->Textured.InnerSpeedX != p1.InnerSpeedX || again->Textured.InnerSpeedY != p1.InnerSpeedY ||
										again->Textured.InnerAutoSpeedX != p1.InnerAutoSpeedX || again->Textured.InnerAutoSpeedY != p1.InnerAutoSpeedY ||
										again->Texture.size() != entry.Texture.size() || !std::equal(entry.Texture.begin(), entry.Texture.end(), again->Texture.begin())) {
										outcome.RoundTripDifference = "layer properties"_s;
										break;
									}
								}
							}
						}
					}
					MemoryStream rewritten(std::int64_t(size) + 4096);
					WriteLevel(rewritten, *level);
					auto again = std::make_unique<LevelData>();
					if (ReadLevel(reinterpret_cast<const std::uint8_t*>(rewritten.GetBuffer()), rewritten.GetSize(), *again) != ReadResult::Success) {
						outcome.RoundTripDifference = "reading again"_s;
					} else if (outcome.RoundTripDifference.empty()) {
						outcome.RoundTripDifference = FindDifference(*level, *again);
					}
				}
			}
			{
				// The properties and the chunks only, the way the list of custom levels reads the preview
				MemoryStream input(data, std::int64_t(size));
				LevelHeader header;
				if (LevelFile::ReadHeader(input, header)) {
					LevelReader reader(input, header);
					LevelProperties properties;
					reader.ReadProperties(properties);
					if (reader.SkipToChunks()) {
						SmallVector<ChunkRecord, 0> chunks;
						reader.ReadChunks(chunks, false);
					}
				}
			}
			{
				// The chunks only checked, the way the game skips them
				MemoryStream input(data, std::int64_t(size));
				LevelHeader header;
				if (LevelFile::ReadHeader(input, header)) {
					LevelReader reader(input, header);
					LevelProperties properties;
					reader.ReadProperties(properties);
					if (reader.SkipToChunks()) {
						reader.SkipChunks();
					}
				}
			}
			return outcome;
		}

		/** Makes a sample of the records of a level, in the form the writer writes them */
		bool FinishLevelSample(FuzzSample& sample, std::unique_ptr<LevelData> level)
		{
			MemoryStream canonical(1024 * 1024);
			if (!WriteLevel(canonical, *level)) {
				return false;
			}
			sample.File.clear();
			AppendStream(sample.File, canonical);
			if (sample.File.size() < LevelHeaderSize || !InflateRaw(sample.File.data() + LevelHeaderSize, sample.File.size() - LevelHeaderSize, sample.Body)) {
				return false;
			}

			// Where the chunks start is where the body of the same level without them ends
			SmallVector<ChunkRecord, 0> chunks = Death::move(level->Chunks);
			level->Chunks.clear();
			MemoryStream bare(1024 * 1024);
			WriteLevel(bare, *level);
			Bytes bareFile, bareBody;
			AppendStream(bareFile, bare);
			if (bareFile.size() < LevelHeaderSize || !InflateRaw(bareFile.data() + LevelHeaderSize, bareFile.size() - LevelHeaderSize, bareBody)) {
				return false;
			}
			sample.ChunksStart = std::min<std::size_t>(bareBody.size(), sample.Body.size());
			level->Chunks = Death::move(chunks);
			sample.Level = Death::move(level);
			return true;
		}

		/** Makes a copy of a sample of a level with a chunk of translated texts made by the writer of the chunk */
		bool MakeLevelTextsSample(const FuzzSample& source, std::uint64_t seed, FuzzSample& sample)
		{
			FuzzRandom random(seed);
			auto level = std::make_unique<LevelData>(*source.Level);
			SetRandomLevelTexts(random, *level, false);
			sample.Path = source.Path;
			sample.Name = String("ltxt-"_s + source.Name);
			sample.IsLevel = true;
			return FinishLevelSample(sample, Death::move(level));
		}

		bool LoadFuzzSample(const String& path, FuzzSample& sample)
		{
			auto s = fs::Open(path, FileAccess::Read);
			if (!s->IsValid()) {
				return false;
			}
			Bytes original;
			original.resize_for_overwrite(std::size_t(s->GetSize()));
			if (s->Read(original.data(), std::int64_t(original.size())) != std::int64_t(original.size())) {
				return false;
			}

			sample.Path = path;
			sample.Name = fs::GetFileName(path);
			sample.IsLevel = (fs::GetExtension(path) == "j2l"_s);
			if (sample.IsLevel) {
				auto level = std::make_unique<LevelData>();
				if (ReadLevel(original.data(), std::int64_t(original.size()), *level) != ReadResult::Success) {
					return false;
				}
				return FinishLevelSample(sample, Death::move(level));
			} else {
				MemoryStream input(original.data(), std::int64_t(original.size()));
				auto tileSet = std::make_unique<TileSetData>();
				if (!TileSetFile::Read(input, *tileSet)) {
					return false;
				}
				sample.File = Death::move(original);
			}
			return true;
		}

		/**
			What the handlers of a crash, a hang or of too much memory need to save the input that caused it; they
			can't allocate or rely on anything else then
		*/
		struct FuzzCrashState {
			const std::uint8_t* Input = nullptr;
			std::size_t InputSize = 0;
#if defined(DEATH_TARGET_WINDOWS)
			wchar_t Directory[1024] = {};
			wchar_t CaseName[256] = {};
#else
			char Directory[2048] = {};
			char CaseName[512] = {};
#endif
			std::atomic<std::int64_t> CaseStartedMs{-1};
			std::atomic<bool> Stop{false};
		};

		FuzzCrashState fuzzState;

		std::int64_t GetFuzzClockMs()
		{
			return std::int64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
		}

		void SaveFuzzInputRaw(const char* reason)
		{
			if (fuzzState.Input == nullptr) {
				return;
			}
#if defined(DEATH_TARGET_WINDOWS)
			wchar_t path[1400];
			swprintf(path, sizeof(path) / sizeof(path[0]), L"%ls\\%hs-%ls", fuzzState.Directory, reason, fuzzState.CaseName);
			std::FILE* f = nullptr;
			if (::_wfopen_s(&f, path, L"wb") != 0) {
				f = nullptr;
			}
			if (f != nullptr) {
				std::fwrite(fuzzState.Input, 1, fuzzState.InputSize, f);
				std::fclose(f);
			}
			std::fprintf(stderr, "Fuzzing found a %s, the input is saved as \"%ls\"\n", reason, path);
#else
			char path[2600];
			std::snprintf(path, sizeof(path), "%s/%s-%s", fuzzState.Directory, reason, fuzzState.CaseName);
			std::FILE* f = std::fopen(path, "wb");
			if (f != nullptr) {
				std::fwrite(fuzzState.Input, 1, fuzzState.InputSize, f);
				std::fclose(f);
			}
			std::fprintf(stderr, "Fuzzing found a %s, the input is saved as \"%s\"\n", reason, path);
#endif
			std::fflush(stderr);
		}

		void OnFuzzSignal(int)
		{
			SaveFuzzInputRaw("crash");
			std::_Exit(3);
		}

#if defined(DEATH_TARGET_WINDOWS)
		LONG WINAPI OnFuzzUnhandledException(EXCEPTION_POINTERS* info)
		{
			// A failed assertion breaks into the debugger in a debug build, which nothing catches without one
			SaveFuzzInputRaw(info->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT ? "assert" : "crash");
			return EXCEPTION_EXECUTE_HANDLER;
		}
#endif

#if !defined(DEATH_TARGET_EMSCRIPTEN)
		void RunFuzzWatchdog(std::int32_t timeoutSeconds, std::int64_t memoryLimit)
		{
			while (!fuzzState.Stop.load()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				const std::int64_t started = fuzzState.CaseStartedMs.load();
				if (started < 0) {
					continue;
				}
				if (GetFuzzClockMs() - started > std::int64_t(timeoutSeconds) * 1000) {
					SaveFuzzInputRaw("hang");
					std::_Exit(4);
				}
#	if defined(DEATH_TARGET_WINDOWS)
				PROCESS_MEMORY_COUNTERS_EX counters = {};
				counters.cb = sizeof(counters);
				if (::K32GetProcessMemoryInfo(::GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)) &&
					std::int64_t(counters.PrivateUsage) > memoryLimit) {
					SaveFuzzInputRaw("memory");
					std::_Exit(5);
				}
#	else
				static_cast<void>(memoryLimit);
#	endif
			}
		}
#endif

#if defined(DEATH_TRACE)
		/** Counts the failed assertions, which only log in a release build */
		class FuzzTraceSink : public Death::ITraceSink
		{
		public:
			std::atomic<std::int32_t> Asserts{0};

		protected:
			void OnTraceReceived(TraceLevel level, std::uint64_t timestamp, StringView threadId, StringView functionName, StringView content) override
			{
				static_cast<void>(timestamp);
				static_cast<void>(threadId);
				static_cast<void>(functionName);
				static_cast<void>(content);
				if (level >= TraceLevel::Assert) {
					Asserts++;
				}
			}

			void OnTraceFlushed() override {}
		};
#endif

		void SetFuzzCase(const Bytes& input, StringView caseName)
		{
			fuzzState.Input = input.data();
			fuzzState.InputSize = input.size();
#if defined(DEATH_TARGET_WINDOWS)
			std::int32_t length = Death::Utf8::ToUtf16(fuzzState.CaseName, caseName.data(), std::int32_t(caseName.size()));
			fuzzState.CaseName[std::min<std::int32_t>(std::max<std::int32_t>(length, 0), std::int32_t(sizeof(fuzzState.CaseName) / sizeof(fuzzState.CaseName[0])) - 1)] = L'\0';
#else
			std::size_t length = std::min<std::size_t>(caseName.size(), sizeof(fuzzState.CaseName) - 1);
			std::memcpy(fuzzState.CaseName, caseName.data(), length);
			fuzzState.CaseName[length] = '\0';
#endif
		}

		void SaveFuzzInput(StringView outputPath, StringView reason, StringView caseName, const Bytes& input)
		{
			String path = fs::CombinePath(outputPath, String(reason + "-"_s + caseName));
			auto s = fs::Open(path, FileAccess::Write);
			if (s->IsValid()) {
				s->Write(input.data(), std::int64_t(input.size()));
			}
			LOGE("Fuzzing found a {} in \"{}\", the input is saved as \"{}\"", reason, caseName, path);
		}
	}

	bool FormatVerifier::FuzzDirectory(StringView path, StringView outputPath, const FuzzOptions& options)
	{
		// A single file is only run through the readers, to reproduce what was found
		if (fs::IsReadableFile(path)) {
			auto s = fs::Open(path, FileAccess::Read);
			Bytes input;
			input.resize_for_overwrite(std::size_t(s->GetSize()));
			if (s->Read(input.data(), std::int64_t(input.size())) != std::int64_t(input.size())) {
				LOGE("Cannot read \"{}\"", path);
				return false;
			}
			// Levels and tile sets start with the same bytes, so the extension (kept by the saved findings) tells them apart
			const bool isLevel = (fs::GetExtension(path) == "j2l"_s);
			FuzzOutcome outcome = (isLevel ? RunLevelReaders(input.data(), input.size()) : RunTileSetReaders(input.data(), input.size()));
			LOGI("\"{}\" was {} as a {}{}{}", path, outcome.Accepted ? "accepted"_s : "refused"_s, isLevel ? "level"_s : "tile set"_s,
				outcome.RoundTripDifference.empty() ? ""_s : ", but it differs after being written again in "_s, outcome.RoundTripDifference);
			return outcome.RoundTripDifference.empty();
		}

		if (!fs::DirectoryExists(path)) {
			LOGE("Directory \"{}\" doesn't exist", path);
			return false;
		}

		SmallVector<String, 0> files;
		CollectFiles(path, "j2l"_s, files);
		CollectFiles(path, "j2t"_s, files);
		SmallVector<std::unique_ptr<FuzzSample>, 0> samples;
		SmallVector<const FuzzSample*, 0> tileSets;
		for (const String& file : files) {
			auto sample = std::make_unique<FuzzSample>();
			if (LoadFuzzSample(file, *sample)) {
				if (!sample->IsLevel) {
					tileSets.push_back(sample.get());
				}
				samples.push_back(Death::move(sample));
			}
		}
		if (samples.empty()) {
			LOGE("No level or tile set in the game's own format found in \"{}\"", path);
			return false;
		}

		// Levels with a chunk of translated texts, made by the writer of the chunk, which none of the converted levels has
		{
			const std::size_t sampleCount = samples.size();
			std::size_t levelIndex = 0;
			for (std::size_t i = 0; i < sampleCount; i++) {
				if (!samples[i]->IsLevel || (!options.FocusLevelTexts && (levelIndex++ % 4) != 0)) {
					continue;
				}
				auto derived = std::make_unique<FuzzSample>();
				if (MakeLevelTextsSample(*samples[i], options.Seed ^ (0xA24BAED4963EE407ull * (i + 1)), *derived)) {
					if (options.FocusLevelTexts) {
						samples[i] = Death::move(derived);
					} else {
						samples.push_back(Death::move(derived));
					}
				}
			}
		}

		if (!fs::CreateDirectories(outputPath)) {
			LOGE("Cannot create \"{}\"", outputPath);
			return false;
		}
		String absoluteOutput = fs::GetAbsolutePath(outputPath);
#if defined(DEATH_TARGET_WINDOWS)
		{
			std::int32_t length = Death::Utf8::ToUtf16(fuzzState.Directory, absoluteOutput.data(), std::int32_t(absoluteOutput.size()));
			fuzzState.Directory[std::min<std::int32_t>(std::max<std::int32_t>(length, 0), std::int32_t(sizeof(fuzzState.Directory) / sizeof(fuzzState.Directory[0])) - 1)] = L'\0';
		}
		::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		::SetUnhandledExceptionFilter(OnFuzzUnhandledException);
#	if defined(DEATH_TARGET_MSVC)
		_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#	endif
#else
		std::snprintf(fuzzState.Directory, sizeof(fuzzState.Directory), "%s", absoluteOutput.data());
#endif
		std::signal(SIGABRT, OnFuzzSignal);
		std::signal(SIGSEGV, OnFuzzSignal);
		std::signal(SIGFPE, OnFuzzSignal);
		std::signal(SIGILL, OnFuzzSignal);

		// With the focus on the translated texts, only the levels are attacked, the tile sets are still embedded into them.
		// Checked before the trace sink and the watchdog are set up below, the loop is the only way past them, which is
		// what takes both down again.
		SmallVector<const FuzzSample*, 0> targets;
		for (const auto& sample : samples) {
			if (sample->IsLevel || !options.FocusLevelTexts) {
				targets.push_back(sample.get());
			}
		}
		if (targets.empty()) {
			LOGE("No level in the game's own format found in \"{}\"", path);
			return false;
		}

#if defined(DEATH_TRACE)
		FuzzTraceSink traceSink;
		Death::Trace::AttachSink(&traceSink);
#endif
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		std::thread watchdog(RunFuzzWatchdog, options.TimeoutSeconds, std::int64_t(options.MemoryLimitMegabytes) * 1024 * 1024);
#endif

		LOGI("Fuzzing {} file(s) with {} inputs, seed {}, findings go to \"{}\"", targets.size(), options.Iterations, options.Seed, absoluteOutput);

		std::int64_t accepted = 0, findings = 0, slowestMs = 0;
		String slowestCase;
#if defined(DEATH_TRACE)
		std::int32_t lastAsserts = 0;
#endif
		const std::int32_t progressStep = std::max<std::int32_t>(options.Iterations / 20, 1);
		const std::int64_t startedMs = GetFuzzClockMs();
		Bytes input;
		for (std::int32_t i = 0; i < options.Iterations; i++) {
			const FuzzSample& sample = *targets[std::size_t(i) % targets.size()];
			// Every input has its own seed, so it's the same with the same seed and index, whatever came before
			FuzzRandom random(options.Seed * 0x9E3779B97F4A7C15ull ^ (std::uint64_t(i) + 1) * 0xD1B54A32D192ED03ull);
			if (sample.IsLevel) {
				MakeLevelInput(random, sample, tileSets, options.FocusLevelTexts, input);
			} else {
				MutateTileSet(random, sample.File, input);
			}

			String caseName = String(std::to_string(i).c_str()) + "-"_s + sample.Name;
			SetFuzzCase(input, caseName);
			const std::int64_t caseStartedMs = GetFuzzClockMs();
			fuzzState.CaseStartedMs.store(caseStartedMs);

			FuzzOutcome outcome = (sample.IsLevel ? RunLevelReaders(input.data(), input.size()) : RunTileSetReaders(input.data(), input.size()));

			const std::int64_t caseMs = GetFuzzClockMs() - caseStartedMs;
			fuzzState.CaseStartedMs.store(-1);
			if (caseMs > slowestMs) {
				slowestMs = caseMs;
				slowestCase = caseName;
			}
			if (outcome.Accepted) {
				accepted++;
			}
			if (!outcome.RoundTripDifference.empty()) {
				SaveFuzzInput(absoluteOutput, String("roundtrip-"_s + outcome.RoundTripDifference), caseName, input);
				findings++;
			}
#if defined(DEATH_TRACE)
			Death::Trace::Flush();
			const std::int32_t asserts = traceSink.Asserts.load();
			if (asserts != lastAsserts) {
				lastAsserts = asserts;
				SaveFuzzInput(absoluteOutput, "assert"_s, caseName, input);
				findings++;
			}
#endif
			if ((i + 1) % progressStep == 0) {
				LOGI("{} of {} inputs tried, {} accepted, {} findings", i + 1, options.Iterations, accepted, findings);
			}
		}

		fuzzState.Input = nullptr;
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		fuzzState.Stop.store(true);
		watchdog.join();
#endif
#if defined(DEATH_TRACE)
		Death::Trace::RemoveSink(&traceSink);
#endif

		LOGI("Fuzzing done in {} s: {} inputs, {} accepted by the readers, {} findings, the slowest input took {} ms ({})",
			(GetFuzzClockMs() - startedMs) / 1000, options.Iterations, accepted, findings, slowestMs, slowestCase);
		return (findings == 0);
	}
}
