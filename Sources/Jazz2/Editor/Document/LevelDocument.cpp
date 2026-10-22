#include "LevelDocument.h"

#if defined(WITH_EDITOR)

#include "Annotations.h"
#include "TerrainSplines.h"
#include "../../EventType.h"
#include "../../Formats/VarUint.h"

#include <algorithm>
#include <atomic>
#include <cstring>

#include <Utf8.h>

// The document knows nothing of the engine, its messages are only marked to be translated where they're shown
#if !defined(NC_)
#	define NC_(context, text) text
#endif

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	namespace
	{
		// A layer larger than this can't be a level anyone made, the size is damaged
		constexpr std::int64_t MaxLayerTiles = std::int64_t(1) << 26;
		// All layers together, a level of many layers of the largest size would only run out of memory
		constexpr std::int64_t MaxLevelTiles = std::int64_t(1) << 27;
		// Tiles converted at once when a layer is written
		constexpr std::int32_t TilesPerBlock = 1024;
		// Version of the layer state chunk this build writes
		constexpr std::uint8_t LayerStateVersion = 1;

		void AppendString8(SmallVectorImpl<std::uint8_t>& data, StringView value)
		{
			// Not in the middle of a character
			std::size_t length = Death::Utf8::FitPrefix(value, UINT8_MAX);
			data.push_back(std::uint8_t(length));
			data.append(reinterpret_cast<const std::uint8_t*>(value.data()), reinterpret_cast<const std::uint8_t*>(value.data()) + length);
		}

		using Formats::ReadVarUint;
		using Formats::WriteVarUint;
	}

	std::uint32_t MakeLayerId()
	{
		// Documents are loaded off the main thread too (by the smart tiles), 0 is never given
		static std::atomic<std::uint32_t> nextId{0};
		std::uint32_t id;
		do {
			id = nextId.fetch_add(1, std::memory_order_relaxed) + 1;
		} while (id == 0);
		return id;
	}

	LevelDocument::LevelDocument()
	{
		Clear();
	}

	void LevelDocument::Clear()
	{
		Flags = LevelFlags::None;
		Properties.DisplayName = {};
		Properties.Author = {};
		Properties.NextLevel = {};
		Properties.SecretLevel = {};
		Properties.BonusLevel = {};
		Properties.DefaultTileSet = {};
		Properties.Music = {};
		Properties.AmbientColor = 0xFFFFFFFFu;
		Properties.Weather = 0;
		Properties.WeatherIntensity = 0;
		Properties.WaterLevel = UINT16_MAX;
		Properties.CaptionTileId = 0;
		std::memset(LevelPalette, 0, sizeof(LevelPalette));
		AdditionalPalettes.clear();
		DefaultTileCount = -1;
		ExtraTileSets.clear();
		DiffuseOverrides.clear();
		MaskOverrides.clear();
		Texts.clear();
		TileIdSpace = DefaultTileIdSpace;
		Animations.clear();
		Layers.clear();
		Events.clear();
		OffGridEvents.clear();
		Chunks.clear();
	}

	void LevelDocument::CreateNew(StringView tileSet, std::int32_t tileCount, Vector2i size)
	{
		Clear();
		Properties.DisplayName = "New Level"_s;
		Properties.DefaultTileSet = tileSet;
		DefaultTileCount = tileCount;

		struct LayerTemplate {
			Formats::LayerKind Kind;
			float Speed;
			std::int16_t Depth;
			bool Repeat;
			std::int32_t Size;
		};
		// A size of 0 is the size of the level
		static const LayerTemplate Templates[] = {
			{ Formats::LayerKind::Other, 1.0f, -100, false, 0 },
			{ Formats::LayerKind::Sprite, 1.0f, 50, false, 0 },
			{ Formats::LayerKind::Other, 0.5f, 100, true, 32 },
			{ Formats::LayerKind::Other, 0.125f, 400, true, 16 },
		};

		for (const LayerTemplate& layerTemplate : Templates) {
			DocumentLayer& layer = Layers.emplace_back();
			std::memset(&layer.Record, 0, sizeof(layer.Record));
			layer.Record.Textured = {};
			layer.Record.Kind = layerTemplate.Kind;
			layer.Record.Flags = Formats::LayerRecord::Visible;
			if (layerTemplate.Repeat) {
				layer.Record.Flags |= Formats::LayerRecord::RepeatX | Formats::LayerRecord::RepeatY;
			}
			layer.Record.Width = (layerTemplate.Size > 0 ? layerTemplate.Size : size.X);
			layer.Record.Height = (layerTemplate.Size > 0 ? layerTemplate.Size : size.Y);
			layer.Record.SpeedX = layerTemplate.Speed;
			layer.Record.SpeedY = layerTemplate.Speed;
			layer.Record.Depth = layerTemplate.Depth;
			std::memset(layer.Record.Color, 255, sizeof(layer.Record.Color));
			layer.Tiles.resize(std::size_t(layer.Record.Width) * std::size_t(layer.Record.Height), TileRef{});
		}
	}

	bool LevelDocument::Load(Stream& s, ITileSetLibrary* tileSets)
	{
		Clear();

		Formats::LevelHeader header;
		if (!Formats::LevelFile::ReadHeader(s, header)) {
			return false;
		}

		Formats::LevelReader reader(s, header);
		// Whether the level has a preview is worked out again when it's saved, from the chunks it has then
		Flags = (header.Flags & ~LevelFlags::HasPreview);

		reader.ReadProperties(Properties);
		if (!reader.ReadPalettes(LevelPalette, &AdditionalPalettes)) {
			std::memset(LevelPalette, 0, sizeof(LevelPalette));
		}
		reader.ReadExtraTileSets(ExtraTileSets);
		reader.ReadTileDiffuseOverrides(&DiffuseOverrides);
		reader.ReadTileMaskOverrides(&MaskOverrides);
		reader.ReadTexts(Texts);

		std::uint16_t animatedTilesOffset = 0;
		SmallVector<Formats::AnimatedTileRecord, 0> animatedTiles;
		reader.ReadAnimatedTiles(animatedTilesOffset, animatedTiles);

		if (tileSets != nullptr && !Properties.DefaultTileSet.empty()) {
			DefaultTileCount = tileSets->GetTileCount(Properties.DefaultTileSet);
		}

		// All animations have to exist before their frames are resolved, a frame can show another animation
		TileIdSpace = std::int32_t(animatedTilesOffset) + std::int32_t(animatedTiles.size());
		Animations.reserve(animatedTiles.size());
		for (Formats::AnimatedTileRecord& record : animatedTiles) {
			DocumentAnimation& animation = Animations.emplace_back();
			animation.Timing = Death::move(record);
		}
		for (DocumentAnimation& animation : Animations) {
			animation.Frames.reserve(animation.Timing.Frames.size());
			for (const Formats::TileRecord& frame : animation.Timing.Frames) {
				animation.Frames.push_back(FromTileId(frame.TileId, frame.Flags));
			}
			animation.Timing.Frames.clear();
		}

		std::int32_t layerCount = reader.ReadLayerCount();
		Layers.reserve(layerCount);
		std::int64_t totalTiles = 0;
		for (std::int32_t i = 0; i < layerCount && reader.IsValid(); i++) {
			DocumentLayer& layer = Layers.emplace_back();
			reader.ReadLayerHeader(layer.Record);
			const std::int64_t tileCount = std::int64_t(layer.Record.Width) * layer.Record.Height;
			totalTiles += tileCount;
			if (!reader.IsValid() || layer.Record.Width < 0 || layer.Record.Height < 0 || tileCount > MaxLayerTiles || totalTiles > MaxLevelTiles) {
				LOGE("Layer {} has a damaged size {}x{}", i, layer.Record.Width, layer.Record.Height);
				Clear();
				return false;
			}

			layer.Tiles.resize_for_overwrite(std::size_t(tileCount));
			reader.ReadLayerTiles(layer.Record, [this, &layer](std::int32_t first, ArrayView<const Formats::TileRecord> tiles) {
				for (std::size_t j = 0; j < tiles.size(); j++) {
					layer.Tiles[first + j] = FromTileId(tiles[j].TileId, tiles[j].Flags);
				}
			});
		}

		Vector2i gridSize = GetEventGridSize();
		reader.ReadEvents(gridSize.X, gridSize.Y, [this, gridSize](std::int32_t x, std::int32_t y, const Formats::EventRecord& event) {
			if (!IsEmptyEvent(event)) {
				// The events come row by row, so they stay sorted by their tile
				DocumentEvent& stored = Events.emplace_back();
				stored.Cell = std::uint32_t(y) * std::uint32_t(gridSize.X) + std::uint32_t(x);
				stored.Record = event;
				NormalizeEvent(stored.Record);
			}
		});

		reader.ReadOffGridEvents(OffGridEvents);
		for (Formats::OffGridEventRecord& event : OffGridEvents) {
			NormalizeEvent(event.Event);
		}

		SmallVector<Formats::ChunkRecord, 0> chunks;
		reader.ReadChunks(chunks, true);

		if (!reader.IsValid()) {
			Clear();
			return false;
		}

		const Formats::ChunkRecord* layerStates = nullptr;
		const Formats::ChunkRecord* legacyLayerStates = nullptr;
		std::int32_t spriteLayer = GetSpriteLayerIndex();
		for (Formats::ChunkRecord& chunk : chunks) {
			if (chunk.Tag == Formats::LayerNamesChunk::Tag) {
				LoadLayerNames(chunk);
			} else if (chunk.Tag == Formats::LayerNumbersChunk::Tag) {
				for (std::int32_t i = 0; i < std::int32_t(Layers.size()); i++) {
					Layers[i].Record.OriginalNumber = Formats::LayerNumbersChunk::Read(chunk.Data, i);
				}
			} else if (chunk.Tag == Formats::SpriteLayerChunk::Tag) {
				// How the sprite layer is drawn is kept in its record as for any other layer, a damaged one leaves the defaults;
				// without a sprite layer it means nothing, and it's written from the record of the sprite layer only
				if (spriteLayer >= 0) {
					Formats::SpriteLayerChunk::Read(chunk.Data, Layers[spriteLayer].Record);
				}
			} else if (chunk.Tag == Formats::TexturedLayerChunk::Tag) {
				// The parameters of the tunnels and the reflections are kept in their records too, and a texture of its own with
				// the layer, a damaged chunk leaves the defaults
				SmallVector<Formats::TexturedLayerChunk::Entry, 4> texturedLayers;
				if (Formats::TexturedLayerChunk::Read(chunk.Data, texturedLayers)) {
					for (Formats::TexturedLayerChunk::Entry& entry : texturedLayers) {
						if (entry.Layer < Layers.size()) {
							Layers[entry.Layer].Record.Textured = entry.Params;
							Layers[entry.Layer].Texture = Death::move(entry.Texture);
						}
					}
				}
			} else if (chunk.Tag == LayerStateChunk) {
				layerStates = &chunk;
			} else if (chunk.Tag == LegacyLayerStateChunk) {
				legacyLayerStates = &chunk;
			} else {
				Chunks.push_back(Death::move(chunk));
			}
		}
		// This version never writes the legacy chunk, so a level that has both was saved by 3.9.0 last, which wrote the
		// flags anew and only kept the other chunk as it was --- the opacity is still taken from that one, the legacy
		// chunk has no room for it
		if (layerStates != nullptr) {
			LoadLayerStates(*layerStates);
		}
		if (legacyLayerStates != nullptr) {
			LoadLegacyLayerStates(*legacyLayerStates);
		}

		// Editor chunks of 3.9.0 are kept in their versioned form from now on, the level is saved with those only
		Annotation::UpgradeLegacyChunk(*this);
		TerrainSpline::UpgradeLegacyChunk(*this);

		return true;
	}

	bool LevelDocument::Save(Stream& s, bool exportOnly, std::int32_t compressionLevel, LevelFlags extraFlags) const
	{
		// The flag tells the list of levels there's a preview at the end of the body before it's read, so it's set
		// exactly when the chunk is written --- it's kept even by an export, as its tag isn't of the editor
		const Formats::ChunkRecord* preview = FindChunk(Formats::LevelFile::PreviewChunk);
		bool hasPreview = (preview != nullptr && !(exportOnly && Formats::LevelFile::IsEditorOnlyChunk(Formats::LevelFile::PreviewChunk)));
		// What the events decide always follows them, whatever the level was loaded with
		LevelFlags flags = ((GetSavedFlags() | extraFlags) & ~LevelFlags::HasPreview);
		if (hasPreview) {
			flags |= LevelFlags::HasPreview;
		}

		Formats::LevelWriter writer(s, flags, compressionLevel);
		writer.WriteProperties(Properties);
		writer.WritePalettes(LevelPalette, AdditionalPalettes);
		writer.WriteExtraTileSets(ExtraTileSets);
		writer.WriteTileDiffuseOverrides(DiffuseOverrides);
		writer.WriteTileMaskOverrides(MaskOverrides);
		writer.WriteTexts(Texts);

		SmallVector<Formats::AnimatedTileRecord, 0> animatedTiles;
		animatedTiles.reserve(Animations.size());
		for (std::size_t i = 0; i < Animations.size(); i++) {
			animatedTiles.push_back(MakeAnimatedTileRecord(std::int32_t(i)));
		}
		writer.WriteAnimatedTiles(std::uint16_t(GetAnimatedTilesOffset()), animatedTiles);

		writer.WriteLayerCount(std::int32_t(Layers.size()));
		Formats::TileRecord block[TilesPerBlock];
		for (const DocumentLayer& layer : Layers) {
			writer.WriteLayerHeader(layer.Record);
			const std::size_t tileCount = layer.Tiles.size();
			for (std::size_t i = 0; i < tileCount; i += TilesPerBlock) {
				const std::size_t count = std::min<std::size_t>(TilesPerBlock, tileCount - i);
				for (std::size_t j = 0; j < count; j++) {
					const TileRef& tile = layer.Tiles[i + j];
					block[j].Flags = tile.Flags;
					block[j].TileId = std::uint16_t(ToTileId(tile));
				}
				writer.WriteLayerTiles(ArrayView<const Formats::TileRecord>(block, count));
			}
		}

		Vector2i gridSize = GetEventGridSize();
		const std::uint32_t cellCount = std::uint32_t(gridSize.X) * std::uint32_t(gridSize.Y);
		const Formats::EventRecord& emptyEvent = GetEmptyEvent();
		std::size_t nextEvent = 0;
		for (std::uint32_t cell = 0; cell < cellCount; cell++) {
			while (nextEvent < Events.size() && Events[nextEvent].Cell < cell) {
				// Events outside of the grid are dropped, the grid can only shrink by an edit command which removes them
				nextEvent++;
			}
			if (nextEvent < Events.size() && Events[nextEvent].Cell == cell) {
				writer.WriteEvent(Events[nextEvent].Record);
				nextEvent++;
			} else {
				writer.WriteEvent(emptyEvent);
			}
		}

		writer.WriteOffGridEvents(OffGridEvents);

		// Chunks the document holds itself come first, then all the others in their order
		SmallVector<Formats::ChunkRecord, 0> chunks;
		std::int32_t spriteLayer = GetSpriteLayerIndex();
		if (spriteLayer >= 0) {
			Formats::ChunkRecord spriteLayerLook;
			spriteLayerLook.Tag = Formats::SpriteLayerChunk::Tag;
			Formats::SpriteLayerChunk::Write(Layers[spriteLayer].Record, spriteLayerLook.Data);
			if (!spriteLayerLook.Data.empty()) {
				chunks.push_back(Death::move(spriteLayerLook));
			}
		}
		SmallVector<Formats::TexturedLayerChunk::Entry, 4> texturedLayers;
		for (std::size_t i = 0; i < Layers.size(); i++) {
			Formats::TexturedLayerChunk::Entry& entry = texturedLayers.emplace_back();
			entry.Layer = std::uint32_t(i);
			entry.Params = Layers[i].Record.Textured;
			entry.Texture = Layers[i].Texture;
		}
		Formats::ChunkRecord texturedLayersChunk;
		texturedLayersChunk.Tag = Formats::TexturedLayerChunk::Tag;
		Formats::TexturedLayerChunk::Write(texturedLayers, texturedLayersChunk.Data);
		if (!texturedLayersChunk.Data.empty()) {
			chunks.push_back(Death::move(texturedLayersChunk));
		}
		Formats::ChunkRecord layerNames;
		layerNames.Tag = Formats::LayerNamesChunk::Tag;
		WriteLayerNames(layerNames.Data);
		if (!layerNames.Data.empty()) {
			chunks.push_back(Death::move(layerNames));
		}
		SmallVector<std::uint8_t, 16> layerNumbers;
		for (const DocumentLayer& layer : Layers) {
			layerNumbers.push_back(layer.Record.OriginalNumber);
		}
		Formats::ChunkRecord layerNumbersChunk;
		layerNumbersChunk.Tag = Formats::LayerNumbersChunk::Tag;
		Formats::LayerNumbersChunk::Write(layerNumbers, layerNumbersChunk.Data);
		if (!layerNumbersChunk.Data.empty()) {
			chunks.push_back(Death::move(layerNumbersChunk));
		}
		if (!exportOnly) {
			Formats::ChunkRecord layerStates;
			layerStates.Tag = LayerStateChunk;
			WriteLayerStates(layerStates.Data);
			if (!layerStates.Data.empty()) {
				chunks.push_back(Death::move(layerStates));
			}
		}
		// The others as they are, without a copy of them, the history of a recovery copy alone can be megabytes
		writer.WriteChunks(chunks, Chunks, exportOnly);

		return writer.Finish();
	}

	std::int32_t LevelDocument::GetSpriteLayerIndex() const
	{
		// The last one wins, as in the game
		for (std::int32_t i = std::int32_t(Layers.size()) - 1; i >= 0; i--) {
			if (Layers[i].IsSpriteLayer()) {
				return i;
			}
		}
		return -1;
	}

	Vector2i LevelDocument::GetEventGridSize() const
	{
		std::int32_t index = GetSpriteLayerIndex();
		if (index < 0) {
			return {};
		}
		return Vector2i(Layers[index].Record.Width, Layers[index].Record.Height);
	}

	std::int32_t LevelDocument::GetAnimatedTilesOffset() const
	{
		// The file stores it in 16 bits, a space of 65536 IDs without any animated tile would be written as 0
		return std::clamp<std::int32_t>(TileIdSpace - std::int32_t(Animations.size()), 0, UINT16_MAX);
	}

	std::int32_t LevelDocument::GetSourceBaseId(std::uint8_t source) const
	{
		if (source == TileRef::DefaultTileSet) {
			return 0;
		}
		if (source == TileRef::Animated) {
			return GetAnimatedTilesOffset();
		}
		if (source == TileRef::Raw || DefaultTileCount < 0 || source > ExtraTileSets.size()) {
			return -1;
		}

		std::int32_t base = DefaultTileCount;
		for (std::int32_t i = 0; i < source - 1; i++) {
			base += ExtraTileSets[i].Count;
		}
		return base;
	}

	std::int32_t LevelDocument::GetSourceTileCount(std::uint8_t source) const
	{
		if (source == TileRef::DefaultTileSet) {
			return DefaultTileCount;
		}
		if (source == TileRef::Animated) {
			return std::int32_t(Animations.size());
		}
		if (source == TileRef::Raw || source > ExtraTileSets.size()) {
			return -1;
		}
		return ExtraTileSets[source - 1].Count;
	}

	std::int32_t LevelDocument::ToTileId(const TileRef& tile) const
	{
		switch (tile.Source) {
			case TileRef::DefaultTileSet:
			case TileRef::Raw:
				return tile.Index;
			case TileRef::Animated:
				return GetAnimatedTilesOffset() + tile.Index;
			default: {
				std::int32_t base = GetSourceBaseId(tile.Source);
				return (base >= 0 ? base + tile.Index : 0);
			}
		}
	}

	TileRef LevelDocument::FromTileId(std::int32_t tileId, std::uint8_t flags) const
	{
		TileRef tile;
		tile.Flags = flags;

		std::int32_t animatedTilesOffset = GetAnimatedTilesOffset();
		if (tileId >= animatedTilesOffset) {
			std::int32_t index = tileId - animatedTilesOffset;
			if (index < std::int32_t(Animations.size())) {
				tile.Index = std::uint16_t(index);
				tile.Source = TileRef::Animated;
			} else {
				tile.Index = std::uint16_t(tileId);
				tile.Source = TileRef::Raw;
			}
			return tile;
		}

		if (ExtraTileSets.empty() || (DefaultTileCount >= 0 && tileId < DefaultTileCount)) {
			// Without extra tile sets, everything below the animated tiles belongs to the default one
			tile.Index = std::uint16_t(tileId);
			tile.Source = TileRef::DefaultTileSet;
			return tile;
		}

		if (DefaultTileCount >= 0) {
			std::int32_t base = DefaultTileCount;
			std::int32_t count = std::min<std::int32_t>(std::int32_t(ExtraTileSets.size()), TileRef::LastExtraTileSet);
			for (std::int32_t i = 0; i < count; i++) {
				if (tileId < base + ExtraTileSets[i].Count) {
					tile.Index = std::uint16_t(tileId - base);
					tile.Source = std::uint8_t(i + 1);
					return tile;
				}
				base += ExtraTileSets[i].Count;
			}
		}

		tile.Index = std::uint16_t(tileId);
		tile.Source = TileRef::Raw;
		return tile;
	}

	LevelDocument::OverrideRefs LevelDocument::CaptureOverrideRefs() const
	{
		OverrideRefs refs;
		refs.Diffuse.reserve(DiffuseOverrides.size());
		for (const Formats::TileDiffuseOverride& tile : DiffuseOverrides) {
			refs.Diffuse.push_back(FromTileId(tile.TileId, 0));
		}
		refs.Mask.reserve(MaskOverrides.size());
		for (const Formats::TileMaskOverride& tile : MaskOverrides) {
			refs.Mask.push_back(FromTileId(tile.TileId, 0));
		}
		return refs;
	}

	std::int32_t LevelDocument::RemapTiles(const OverrideRefs& overrides, Function<bool(TileRef&)>& remap)
	{
		// A tile is gone if it's remapped away, or if it's past the end of its tile set as it is now
		auto apply = [this, &remap](TileRef& tile) {
			if (!remap(tile)) {
				return false;
			}
			if (tile.Source == TileRef::Raw || tile.Source == TileRef::Animated) {
				return true;
			}
			std::int32_t count = GetSourceTileCount(tile.Source);
			if (tile.Source != TileRef::DefaultTileSet && count < 0) {
				return false;
			}
			return (count < 0 || tile.Index < count);
		};

		std::int32_t cleared = 0;
		for (DocumentLayer& layer : Layers) {
			for (TileRef& tile : layer.Tiles) {
				if (!tile.IsEmpty() && !apply(tile)) {
					tile = TileRef{};
					cleared++;
				}
			}
		}
		for (DocumentAnimation& animation : Animations) {
			for (TileRef& frame : animation.Frames) {
				if (!frame.IsEmpty() && !apply(frame)) {
					frame = TileRef{};
				}
			}
		}

		// Ground lines remember the tiles they wrote and those that were there before, they follow the tiles too,
		// so a line still knows its tiles when it's laid again
		Function<bool(TileRef&)> applyToLines = [&apply](TileRef& tile) {
			return (tile.IsEmpty() || apply(tile));
		};
		TerrainSpline::RemapTiles(*this, applyToLines);

		// Overrides are kept by tile ID, they're put back by the new IDs of their tiles, sorted as the file has them
		auto remapOverrides = [this, &apply](auto& list, const SmallVector<TileRef, 0>& refs) {
			std::size_t kept = 0;
			for (std::size_t i = 0; i < list.size() && i < refs.size(); i++) {
				TileRef tile = refs[i];
				if (tile.Source == TileRef::Animated || !apply(tile)) {
					continue;
				}
				list[i].TileId = std::uint16_t(ToTileId(tile));
				if (kept != i) {
					list[kept] = Death::move(list[i]);
				}
				kept++;
			}
			list.erase(list.begin() + kept, list.end());
			std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.TileId < b.TileId; });
		};
		remapOverrides(DiffuseOverrides, overrides.Diffuse);
		remapOverrides(MaskOverrides, overrides.Mask);
		return cleared;
	}

	bool LevelDocument::ResizeLayer(std::int32_t layerIndex, Vector2i size, Vector2i offset, ResizeFill fill)
	{
		if (layerIndex < 0 || layerIndex >= std::int32_t(Layers.size()) || size.X <= 0 || size.Y <= 0 ||
			size.X > Formats::LevelFile::MaxLayerSize || size.Y > Formats::LevelFile::MaxLayerSize || std::int64_t(size.X) * size.Y > MaxLayerTiles) {
			return false;
		}

		DocumentLayer& layer = Layers[layerIndex];
		std::int32_t oldWidth = layer.GetWidth(), oldHeight = layer.GetHeight();
		SmallVector<TileRef, 0> tiles;
		tiles.resize_for_overwrite(std::size_t(size.X) * std::size_t(size.Y));
		for (std::int32_t y = 0; y < size.Y; y++) {
			for (std::int32_t x = 0; x < size.X; x++) {
				std::int32_t sx = x - offset.X, sy = y - offset.Y;
				TileRef tile = {};
				if (sx >= 0 && sx < oldWidth && sy >= 0 && sy < oldHeight) {
					tile = layer.GetTile(sx, sy);
				} else if (fill == ResizeFill::ExtendEdges && oldWidth > 0 && oldHeight > 0) {
					tile = layer.GetTile(std::clamp<std::int32_t>(sx, 0, oldWidth - 1), std::clamp<std::int32_t>(sy, 0, oldHeight - 1));
				} else if (fill == ResizeFill::Repeat && oldWidth > 0 && oldHeight > 0) {
					tile = layer.GetTile(((sx % oldWidth) + oldWidth) % oldWidth, ((sy % oldHeight) + oldHeight) % oldHeight);
				}
				tiles[std::size_t(y) * std::size_t(size.X) + std::size_t(x)] = tile;
			}
		}
		layer.Tiles = Death::move(tiles);
		layer.Record.Width = size.X;
		layer.Record.Height = size.Y;

		if (layer.IsSpriteLayer()) {
			// The events are on the tiles of the sprite layer, their cells are counted by its width
			SmallVector<DocumentEvent, 0> events;
			events.reserve(Events.size());
			for (const DocumentEvent& event : Events) {
				std::int32_t x = std::int32_t(event.Cell % std::uint32_t(oldWidth)) + offset.X;
				std::int32_t y = std::int32_t(event.Cell / std::uint32_t(oldWidth)) + offset.Y;
				if (x >= 0 && x < size.X && y >= 0 && y < size.Y) {
					DocumentEvent& moved = events.emplace_back(event);
					moved.Cell = std::uint32_t(y) * std::uint32_t(size.X) + std::uint32_t(x);
				}
			}
			std::sort(events.begin(), events.end(), [](const DocumentEvent& a, const DocumentEvent& b) { return a.Cell < b.Cell; });
			Events = Death::move(events);

			constexpr std::int32_t TileSize = Formats::LevelFile::TileSize;
			for (std::size_t i = 0; i < OffGridEvents.size(); ) {
				std::int32_t x = std::int32_t(OffGridEvents[i].X) + offset.X * TileSize;
				std::int32_t y = std::int32_t(OffGridEvents[i].Y) + offset.Y * TileSize;
				if (x < 0 || y < 0 || x >= size.X * TileSize || y >= size.Y * TileSize) {
					OffGridEvents.erase(OffGridEvents.begin() + i);
					continue;
				}
				OffGridEvents[i].X = std::uint32_t(x);
				OffGridEvents[i].Y = std::uint32_t(y);
				i++;
			}

			// Water below the level means no water, which stays so
			if (std::int32_t(Properties.WaterLevel) < oldHeight * TileSize) {
				Properties.WaterLevel = std::uint16_t(std::clamp<std::int32_t>(std::int32_t(Properties.WaterLevel) + offset.Y * TileSize, 0, UINT16_MAX));
			}
		}
		return true;
	}

	std::int32_t LevelDocument::GetStaticTileCount() const
	{
		if (DefaultTileCount < 0) {
			return -1;
		}
		std::int32_t count = DefaultTileCount;
		for (const Formats::ExtraTileSetRecord& tileSet : ExtraTileSets) {
			count += tileSet.Count;
		}
		return count;
	}

	void LevelDocument::FitTileIdSpace()
	{
		std::int32_t staticCount = GetStaticTileCount();
		if (staticCount > GetAnimatedTilesOffset()) {
			// Tiles past the space can't be used, Validate() tells about those the level uses
			TileIdSpace = std::min<std::int32_t>(staticCount + std::int32_t(Animations.size()), UINT16_MAX + 1);
		}
	}

	LevelFlags LevelDocument::GetFlagsFromEvents() const
	{
		// As the converter of the original levels decides them, see JJ2Level::Convert(), but off the grid too, where
		// the game takes them the same --- an event a generator makes doesn't count, it's only made later
		bool hasLaps = false, hasCtfBase = false, hasMultiplayerStart = false;
		auto check = [&](const Formats::EventRecord& event) {
			if ((event.Flags & Formats::EventRecord::Generator) != 0) {
				return;
			}
			switch (EventType(event.Type)) {
				case EventType::WarpOrigin:
				case EventType::WarpCoinBonus:
					// "Set Lap", the finish line of a race
					hasLaps |= (event.Params[2] != 0);
					break;
				case EventType::CtfBase:
					hasCtfBase = true;
					break;
				case EventType::LevelStartMultiplayer:
					hasMultiplayerStart = true;
					break;
				default:
					break;
			}
		};
		for (const DocumentEvent& event : Events) {
			check(event.Record);
		}
		for (const Formats::OffGridEventRecord& event : OffGridEvents) {
			check(event.Event);
		}

		LevelFlags flags = LevelFlags::None;
		// Races and Capture the Flag only in a level for multiplayer
		if ((Flags & LevelFlags::IsMultiplayerLevel) == LevelFlags::IsMultiplayerLevel) {
			if (hasLaps) {
				flags |= LevelFlags::HasLaps;
			}
			if (hasCtfBase) {
				flags |= LevelFlags::HasCaptureTheFlag;
			}
		}
		if (hasMultiplayerStart) {
			flags |= LevelFlags::HasMultiplayerSpawnPoints;
		}
		return flags;
	}

	LevelFlags LevelDocument::GetSavedFlags() const
	{
		return ((Flags & ~EventFlags) | GetFlagsFromEvents());
	}

	bool LevelDocument::IsTileAddressable(const TileRef& tile) const
	{
		if (tile.IsEmpty() || tile.Source == TileRef::Animated || tile.Source == TileRef::Raw) {
			return true;
		}
		return (ToTileId(tile) < GetAnimatedTilesOffset());
	}

	Formats::AnimatedTileRecord LevelDocument::MakeAnimatedTileRecord(std::int32_t index) const
	{
		const DocumentAnimation& animation = Animations[index];
		Formats::AnimatedTileRecord record;
		record.FrameDuration16 = animation.Timing.FrameDuration16;
		record.Delay = animation.Timing.Delay;
		record.DelayJitter = animation.Timing.DelayJitter;
		record.PingPongDelay = animation.Timing.PingPongDelay;
		record.IsPingPong = animation.Timing.IsPingPong;
		record.Frames.reserve(animation.Frames.size());
		for (const TileRef& frame : animation.Frames) {
			Formats::TileRecord& tile = record.Frames.emplace_back();
			tile.Flags = frame.Flags;
			tile.TileId = std::uint16_t(ToTileId(frame));
		}
		return record;
	}

	const Formats::EventRecord& LevelDocument::GetEvent(std::int32_t x, std::int32_t y) const
	{
		Vector2i gridSize = GetEventGridSize();
		if (x < 0 || y < 0 || x >= gridSize.X || y >= gridSize.Y) {
			return GetEmptyEvent();
		}

		std::uint32_t cell = std::uint32_t(y) * std::uint32_t(gridSize.X) + std::uint32_t(x);
		std::size_t index = FindEvent(cell);
		if (index < Events.size() && Events[index].Cell == cell) {
			return Events[index].Record;
		}
		return GetEmptyEvent();
	}

	void LevelDocument::SetEvent(std::int32_t x, std::int32_t y, const Formats::EventRecord& event)
	{
		Vector2i gridSize = GetEventGridSize();
		if (x < 0 || y < 0 || x >= gridSize.X || y >= gridSize.Y) {
			return;
		}

		std::uint32_t cell = std::uint32_t(y) * std::uint32_t(gridSize.X) + std::uint32_t(x);
		std::size_t index = FindEvent(cell);
		bool exists = (index < Events.size() && Events[index].Cell == cell);

		if (IsEmptyEvent(event)) {
			if (exists) {
				Events.erase(Events.begin() + index);
			}
			return;
		}

		if (exists) {
			Events[index].Record = event;
			NormalizeEvent(Events[index].Record);
		} else {
			DocumentEvent stored;
			stored.Cell = cell;
			stored.Record = event;
			NormalizeEvent(stored.Record);
			Events.insert(Events.begin() + index, stored);
		}
	}

	void LevelDocument::SetEvents(ArrayView<const DocumentEvent> events)
	{
		Vector2i gridSize = GetEventGridSize();
		const std::uint32_t cellCount = std::uint32_t(gridSize.X) * std::uint32_t(gridSize.Y);

		SmallVector<DocumentEvent, 0> merged;
		merged.reserve(Events.size() + events.size());
		std::size_t i = 0, j = 0;
		while (i < Events.size() || j < events.size()) {
			if (j >= events.size() || (i < Events.size() && Events[i].Cell < events[j].Cell)) {
				merged.push_back(Events[i]);
				i++;
				continue;
			}
			// The new one replaces the one at the same tile, if there's any
			if (i < Events.size() && Events[i].Cell == events[j].Cell) {
				i++;
			}
			const DocumentEvent& event = events[j];
			j++;
			// Skipped as by SetEvent(), and so is one of a tile listed again, which would break the order
			if (event.Cell >= cellCount || (!merged.empty() && merged.back().Cell >= event.Cell) || IsEmptyEvent(event.Record)) {
				continue;
			}
			DocumentEvent& stored = merged.emplace_back(event);
			NormalizeEvent(stored.Record);
		}
		Events = Death::move(merged);
	}

	std::size_t LevelDocument::FindEvent(std::uint32_t cell) const
	{
		auto it = std::lower_bound(Events.begin(), Events.end(), cell, [](const DocumentEvent& event, std::uint32_t cell) {
			return event.Cell < cell;
		});
		return std::size_t(it - Events.begin());
	}

	const Formats::EventRecord& LevelDocument::GetEmptyEvent()
	{
		static const Formats::EventRecord emptyEvent = [] {
			Formats::EventRecord event;
			std::memset(&event, 0, sizeof(event));
			event.Flags = Formats::EventRecord::DifficultyMask;
			return event;
		}();
		return emptyEvent;
	}

	bool LevelDocument::IsEmptyEvent(const Formats::EventRecord& event)
	{
		return AreEventsEqual(event, GetEmptyEvent());
	}

	bool LevelDocument::AreEventsEqual(const Formats::EventRecord& a, const Formats::EventRecord& b)
	{
		if (a.Type != b.Type || (a.Flags & ~Formats::EventRecord::NoParams) != (b.Flags & ~Formats::EventRecord::NoParams)) {
			return false;
		}
		if ((a.Flags & Formats::EventRecord::Generator) != 0 &&
			(a.GeneratorFlags != b.GeneratorFlags || a.GeneratorDelay != b.GeneratorDelay)) {
			return false;
		}
		// The parameters of an event without a type are never written
		return (a.Type == 0 || std::memcmp(a.Params, b.Params, sizeof(a.Params)) == 0);
	}

	void LevelDocument::NormalizeEvent(Formats::EventRecord& event)
	{
		event.Flags &= ~Formats::EventRecord::NoParams;
		if ((event.Flags & Formats::EventRecord::Generator) == 0) {
			event.GeneratorFlags = 0;
			event.GeneratorDelay = 0;
		}
		if (event.Type == 0) {
			std::memset(event.Params, 0, sizeof(event.Params));
		}
	}

	const Formats::ChunkRecord* LevelDocument::FindChunk(std::uint32_t tag) const
	{
		for (const Formats::ChunkRecord& chunk : Chunks) {
			if (chunk.Tag == tag) {
				return &chunk;
			}
		}
		return nullptr;
	}

	void LevelDocument::SetChunk(std::uint32_t tag, ArrayView<const std::uint8_t> data)
	{
		DEATH_DEBUG_ASSERT(tag != Formats::LayerNamesChunk::Tag && tag != Formats::LayerNumbersChunk::Tag && tag != LayerStateChunk && tag != LegacyLayerStateChunk &&
			tag != Formats::SpriteLayerChunk::Tag && tag != Formats::TexturedLayerChunk::Tag, "The chunk is held by the document itself", );

		for (std::size_t i = 0; i < Chunks.size(); i++) {
			if (Chunks[i].Tag == tag) {
				if (data.empty()) {
					Chunks.erase(Chunks.begin() + i);
				} else {
					Chunks[i].Data.assign(data.begin(), data.end());
				}
				return;
			}
		}

		if (!data.empty()) {
			Formats::ChunkRecord& chunk = Chunks.emplace_back();
			chunk.Tag = tag;
			chunk.Data.assign(data.begin(), data.end());
		}
	}

	void LevelDocument::ReplaceChunk(std::uint32_t tag, SmallVector<std::uint8_t, 0>&& data)
	{
		DEATH_DEBUG_ASSERT(tag != Formats::LayerNamesChunk::Tag && tag != Formats::LayerNumbersChunk::Tag && tag != LayerStateChunk && tag != LegacyLayerStateChunk &&
			tag != Formats::SpriteLayerChunk::Tag && tag != Formats::TexturedLayerChunk::Tag, "The chunk is held by the document itself", );

		for (std::size_t i = 0; i < Chunks.size(); i++) {
			if (Chunks[i].Tag == tag) {
				if (data.empty()) {
					Chunks.erase(Chunks.begin() + i);
				} else {
					Chunks[i].Data = Death::move(data);
				}
				return;
			}
		}

		if (!data.empty()) {
			Formats::ChunkRecord& chunk = Chunks.emplace_back();
			chunk.Tag = tag;
			chunk.Data = Death::move(data);
		}
	}

	bool LevelDocument::Validate(String* problem) const
	{
		auto fail = [problem](String&& message) {
			if (problem != nullptr) {
				*problem = Death::move(message);
			}
			return false;
		};

		std::int32_t spriteLayers = 0;
		for (const DocumentLayer& layer : Layers) {
			if (layer.IsSpriteLayer()) {
				spriteLayers++;
			}
			if (layer.Tiles.size() != std::size_t(layer.Record.Width) * std::size_t(layer.Record.Height)) {
				return fail(NC_("editor", "A layer has a different number of tiles than its size"));
			}
		}
		if (spriteLayers != 1) {
			return fail(NC_("editor", "The level has to have exactly one sprite layer"));
		}
		if (Layers.size() > UINT8_MAX) {
			return fail(NC_("editor", "The level has too many layers"));
		}
		if (Texts.size() > UINT8_MAX) {
			return fail(NC_("editor", "The level has too many texts"));
		}
		if (ExtraTileSets.size() > TileRef::LastExtraTileSet) {
			return fail(NC_("editor", "The level has too many extra tile sets"));
		}
		if (TileIdSpace > UINT16_MAX + 1 || std::int32_t(Animations.size()) > TileIdSpace) {
			return fail(NC_("editor", "The level has too many animated tiles"));
		}
		for (const DocumentAnimation& animation : Animations) {
			if (animation.Frames.size() > UINT8_MAX) {
				return fail(NC_("editor", "An animated tile has too many frames"));
			}
		}
		// Tile sets with more tiles than the level can address are allowed, the game doesn't mind the tiles that
		// can't be used, so it's left to the lint --- but a tile that is used there would be saved as an ID of an
		// animated tile, or one that doesn't fit, and the level would show something else (the editor grows the space
		// before, see ExecuteGrowingTileIdSpace())
		for (const DocumentLayer& layer : Layers) {
			for (const TileRef& tile : layer.Tiles) {
				if (!IsTileAddressable(tile)) {
					return fail(NC_("editor", "The level uses tiles past the tile IDs it can address"));
				}
			}
		}
		for (const DocumentAnimation& animation : Animations) {
			for (const TileRef& frame : animation.Frames) {
				if (!IsTileAddressable(frame)) {
					return fail(NC_("editor", "The level uses tiles past the tile IDs it can address"));
				}
			}
		}
		return true;
	}

	void LevelDocument::LoadLayerNames(const Formats::ChunkRecord& chunk)
	{
		for (std::int32_t i = 0; i < std::int32_t(Layers.size()); i++) {
			Layers[i].Name = Formats::LayerNamesChunk::Read(chunk.Data, i);
		}
	}

	bool LevelDocument::LoadLayerStates(const Formats::ChunkRecord& chunk)
	{
		const std::uint8_t* p = chunk.Data.data();
		const std::uint8_t* end = p + chunk.Data.size();
		std::uint32_t count, stride;
		// A newer version only adds bytes to each layer, so any version but 0 is read
		if (p >= end || *p++ == 0 || !ReadVarUint(p, end, count) || !ReadVarUint(p, end, stride) || stride == 0) {
			LOGW("Editor state of layers is damaged");
			return false;
		}
		for (std::uint32_t i = 0; i < count && i < Layers.size(); i++) {
			if (std::size_t(end - p) < stride) {
				break;
			}
			Layers[i].EditorFlags = p[0];
			Layers[i].EditorOpacity = (stride >= 2 ? p[1] : 255);
			p += stride;
		}
		return true;
	}

	void LevelDocument::LoadLegacyLayerStates(const Formats::ChunkRecord& chunk)
	{
		std::size_t count = std::min<std::size_t>(chunk.Data.size(), Layers.size());
		for (std::size_t i = 0; i < count; i++) {
			Layers[i].EditorFlags = chunk.Data[i];
		}
	}

	void LevelDocument::WriteLayerNames(SmallVectorImpl<std::uint8_t>& data) const
	{
		SmallVector<StringView, 16> names;
		for (const DocumentLayer& layer : Layers) {
			names.push_back(layer.Name);
		}
		Formats::LayerNamesChunk::Write(names, data);
	}

	void LevelDocument::WriteLayerStates(SmallVectorImpl<std::uint8_t>& data) const
	{
		bool anyState = false, anyOpacity = false;
		for (const DocumentLayer& layer : Layers) {
			anyState |= (layer.EditorFlags != 0);
			anyOpacity |= (layer.EditorOpacity != 255);
		}
		if (!anyState && !anyOpacity) {
			return;
		}

		data.push_back(LayerStateVersion);
		WriteVarUint(data, std::uint32_t(Layers.size()));
		// The flags, and the opacity only if a layer has one, a reader of 1 byte for each layer skips it
		WriteVarUint(data, anyOpacity ? 2 : 1);
		for (const DocumentLayer& layer : Layers) {
			data.push_back(layer.EditorFlags);
			if (anyOpacity) {
				data.push_back(layer.EditorOpacity);
			}
		}
	}
}

#endif
