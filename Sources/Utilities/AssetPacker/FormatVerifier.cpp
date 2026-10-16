#include "FormatVerifier.h"

#include "../../Jazz2/Formats/LevelFile.h"
#include "../../Jazz2/Formats/TileSetFile.h"

#include <Containers/String.h>
#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>
#include <IO/MemoryStream.h>

#include <cstring>

using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::IO;
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
			for (std::int32_t i = 0; i < layerCount; i++) {
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
				return ReadResult::Failed;
			}

			level.Events.reserve(std::size_t(spriteLayer->Header.Width) * std::size_t(spriteLayer->Header.Height));
			reader.ReadEvents(spriteLayer->Header.Width, spriteLayer->Header.Height, [&level](std::int32_t, std::int32_t, const EventRecord& event) {
				level.Events.push_back(event);
			});
			reader.ReadOffGridEvents(level.OffGridEvents);
			reader.ReadChunks(level.Chunks, true);

			return (reader.IsValid() ? ReadResult::Success : ReadResult::Failed);
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
			// NoParams is derived from the parameters by the writer, so another writer may have left it out
			return (a.Type == b.Type && (a.Flags & ~EventRecord::NoParams) == (b.Flags & ~EventRecord::NoParams) &&
				a.GeneratorFlags == b.GeneratorFlags && a.GeneratorDelay == b.GeneratorDelay &&
				std::memcmp(a.Params, b.Params, sizeof(a.Params)) == 0);
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
			if (a.Header.Flags != b.Header.Flags) {
				return "header"_s;
			}

			const LevelProperties& pa = a.Properties;
			const LevelProperties& pb = b.Properties;
			if (pa.DisplayName != pb.DisplayName || pa.NextLevel != pb.NextLevel || pa.SecretLevel != pb.SecretLevel ||
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
				if (a.Chunks[i].Tag != b.Chunks[i].Tag || a.Chunks[i].Data.size() != b.Chunks[i].Data.size() ||
					std::memcmp(a.Chunks[i].Data.data(), b.Chunks[i].Data.data(), a.Chunks[i].Data.size()) != 0) {
					return "chunks"_s;
				}
			}

			return {};
		}

		VerifyResult VerifyLevel(StringView path)
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
				case ReadResult::Failed: LOGE("Level \"{}\" cannot be read", path); return VerifyResult::Failed;
				default: break;
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

		VerifyResult VerifyTileSet(StringView path)
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

			TileSetHeader header;
			{
				MemoryStream input(original.data(), std::int64_t(original.size()));
				if (!TileSetFile::ReadHeader(input, header)) {
					return VerifyResult::NotNative;
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

	bool FormatVerifier::VerifyDirectory(StringView path)
	{
		if (!fs::DirectoryExists(path)) {
			LOGE("Directory \"{}\" doesn't exist", path);
			return false;
		}

		SmallVector<String, 0> levels;
		CollectFiles(path, "j2l"_s, levels);
		VerifyCounts levelCounts;
		for (const String& level : levels) {
			levelCounts.Add(VerifyLevel(level));
		}

		SmallVector<String, 0> tileSets;
		CollectFiles(path, "j2t"_s, tileSets);
		VerifyCounts tileSetCounts;
		for (const String& tileSet : tileSets) {
			tileSetCounts.Add(VerifyTileSet(tileSet));
		}

		LOGI("{} level(s) verified: {} identical, {} with the same content written differently, {} different, {} failed, {} not in the native format",
			levelCounts.Identical + levelCounts.Equivalent + levelCounts.Different + levelCounts.Failed, levelCounts.Identical,
			levelCounts.Equivalent, levelCounts.Different, levelCounts.Failed, levelCounts.NotNative);
		LOGI("{} tile set(s) verified: {} identical, {} with the same content written differently, {} different, {} failed, {} not in the native format",
			tileSetCounts.Identical + tileSetCounts.Equivalent + tileSetCounts.Different + tileSetCounts.Failed, tileSetCounts.Identical,
			tileSetCounts.Equivalent, tileSetCounts.Different, tileSetCounts.Failed, tileSetCounts.NotNative);
		return (levelCounts.Different == 0 && levelCounts.Failed == 0 && tileSetCounts.Different == 0 && tileSetCounts.Failed == 0);
	}
}
