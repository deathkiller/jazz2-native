#include "LevelDocument.h"

#if defined(WITH_EDITOR)

#include <algorithm>
#include <cstring>

// The document knows nothing of the engine, its messages are only marked to be translated where they're shown
#if !defined(N_)
#	define N_(text) text
#endif

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	namespace
	{
		// A layer larger than this can't be a level anyone made, the size is damaged
		constexpr std::int64_t MaxLayerTiles = std::int64_t(1) << 26;
		// Tiles converted at once when a layer is written
		constexpr std::int32_t TilesPerBlock = 1024;

		void AppendString8(SmallVectorImpl<std::uint8_t>& data, StringView value)
		{
			std::size_t length = std::min<std::size_t>(value.size(), UINT8_MAX);
			data.push_back(std::uint8_t(length));
			data.append(reinterpret_cast<const std::uint8_t*>(value.data()), reinterpret_cast<const std::uint8_t*>(value.data()) + length);
		}
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
		Flags = header.Flags;

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
			animation.Timing = std::move(record);
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
		for (std::int32_t i = 0; i < layerCount && reader.IsValid(); i++) {
			DocumentLayer& layer = Layers.emplace_back();
			reader.ReadLayerHeader(layer.Record);
			const std::int64_t tileCount = std::int64_t(layer.Record.Width) * layer.Record.Height;
			if (!reader.IsValid() || tileCount > MaxLayerTiles) {
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

		for (Formats::ChunkRecord& chunk : chunks) {
			if (chunk.Tag == Formats::LevelFile::LayerNamesChunk) {
				LoadLayerNames(chunk);
			} else if (chunk.Tag == LayerStateChunk) {
				LoadLayerStates(chunk);
			} else {
				Chunks.push_back(std::move(chunk));
			}
		}

		return true;
	}

	bool LevelDocument::Save(Stream& s, bool exportOnly, std::int32_t compressionLevel, LevelFlags extraFlags) const
	{
		Formats::LevelWriter writer(s, Flags | extraFlags, compressionLevel);
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
		Formats::ChunkRecord layerNames;
		layerNames.Tag = Formats::LevelFile::LayerNamesChunk;
		WriteLayerNames(layerNames.Data);
		if (!layerNames.Data.empty()) {
			chunks.push_back(std::move(layerNames));
		}
		if (!exportOnly) {
			Formats::ChunkRecord layerStates;
			layerStates.Tag = LayerStateChunk;
			WriteLayerStates(layerStates.Data);
			if (!layerStates.Data.empty()) {
				chunks.push_back(std::move(layerStates));
			}
		}
		for (const Formats::ChunkRecord& chunk : Chunks) {
			chunks.push_back(chunk);
		}
		writer.WriteChunks(chunks, exportOnly);

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
		return std::max(TileIdSpace - std::int32_t(Animations.size()), 0);
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
					list[kept] = std::move(list[i]);
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
			size.X > UINT16_MAX || size.Y > UINT16_MAX || std::int64_t(size.X) * size.Y > MaxLayerTiles) {
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
					tile = layer.GetTile(std::clamp(sx, 0, oldWidth - 1), std::clamp(sy, 0, oldHeight - 1));
				} else if (fill == ResizeFill::Repeat && oldWidth > 0 && oldHeight > 0) {
					tile = layer.GetTile(((sx % oldWidth) + oldWidth) % oldWidth, ((sy % oldHeight) + oldHeight) % oldHeight);
				}
				tiles[std::size_t(y) * std::size_t(size.X) + std::size_t(x)] = tile;
			}
		}
		layer.Tiles = std::move(tiles);
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
			Events = std::move(events);

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
				Properties.WaterLevel = std::uint16_t(std::clamp(std::int32_t(Properties.WaterLevel) + offset.Y * TileSize, 0, std::int32_t(UINT16_MAX)));
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
			TileIdSpace = std::min(staticCount + std::int32_t(Animations.size()), UINT16_MAX + 1);
		}
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
		DEATH_DEBUG_ASSERT(tag != Formats::LevelFile::LayerNamesChunk && tag != LayerStateChunk,
			"The chunk is held by the document itself", );

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

	bool LevelDocument::Validate(String* problem) const
	{
		auto fail = [problem](String&& message) {
			if (problem != nullptr) {
				*problem = std::move(message);
			}
			return false;
		};

		std::int32_t spriteLayers = 0;
		for (const DocumentLayer& layer : Layers) {
			if (layer.IsSpriteLayer()) {
				spriteLayers++;
			}
			if (layer.Tiles.size() != std::size_t(layer.Record.Width) * std::size_t(layer.Record.Height)) {
				return fail(N_("A layer has a different number of tiles than its size"));
			}
		}
		if (spriteLayers != 1) {
			return fail(N_("The level has to have exactly one sprite layer"));
		}
		if (Layers.size() > UINT8_MAX) {
			return fail(N_("The level has too many layers"));
		}
		if (Texts.size() > UINT8_MAX) {
			return fail(N_("The level has too many texts"));
		}
		if (ExtraTileSets.size() > TileRef::LastExtraTileSet) {
			return fail(N_("The level has too many extra tile sets"));
		}
		if (TileIdSpace > UINT16_MAX + 1 || std::int32_t(Animations.size()) > TileIdSpace) {
			return fail(N_("The level has too many animated tiles"));
		}
		for (const DocumentAnimation& animation : Animations) {
			if (animation.Frames.size() > UINT8_MAX) {
				return fail(N_("An animated tile has too many frames"));
			}
		}
		// Tile sets with more tiles than the level can address are allowed, the game doesn't mind the tiles that
		// can't be used, so it's left to the lint
		return true;
	}

	void LevelDocument::LoadLayerNames(const Formats::ChunkRecord& chunk)
	{
		std::size_t pos = 0;
		for (DocumentLayer& layer : Layers) {
			if (pos >= chunk.Data.size()) {
				break;
			}
			std::size_t length = chunk.Data[pos++];
			length = std::min(length, chunk.Data.size() - pos);
			layer.Name = String(reinterpret_cast<const char*>(chunk.Data.data() + pos), length);
			pos += length;
		}
	}

	void LevelDocument::LoadLayerStates(const Formats::ChunkRecord& chunk)
	{
		std::size_t count = std::min(chunk.Data.size(), Layers.size());
		for (std::size_t i = 0; i < count; i++) {
			Layers[i].EditorFlags = chunk.Data[i];
		}
	}

	void LevelDocument::WriteLayerNames(SmallVectorImpl<std::uint8_t>& data) const
	{
		bool anyName = false;
		for (const DocumentLayer& layer : Layers) {
			if (!layer.Name.empty()) {
				anyName = true;
				break;
			}
		}
		if (!anyName) {
			return;
		}

		for (const DocumentLayer& layer : Layers) {
			AppendString8(data, layer.Name);
		}
	}

	void LevelDocument::WriteLayerStates(SmallVectorImpl<std::uint8_t>& data) const
	{
		bool anyState = false;
		for (const DocumentLayer& layer : Layers) {
			if (layer.EditorFlags != 0) {
				anyState = true;
				break;
			}
		}
		if (!anyState) {
			return;
		}

		for (const DocumentLayer& layer : Layers) {
			data.push_back(layer.EditorFlags);
		}
	}
}

#endif
