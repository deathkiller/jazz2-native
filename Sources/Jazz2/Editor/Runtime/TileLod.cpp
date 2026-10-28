#include "TileLod.h"

#if defined(WITH_EDITOR)

#include "RuntimeSync.h"
#include "../Document/DocumentChanges.h"
#include "../Files/TileSetDataCache.h"
#include "../Imaging/LevelThumbnail.h"
#include "../Common/ToolMath.h"
#include "../../ContentResolver.h"
#include "../../../nCine/Base/TimeStamp.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Jazz2::Tiles;
using namespace nCine;

namespace Jazz2::Editor
{
	namespace
	{
		// Most memory the images take, those drawn the longest time ago go first; an image is at most 1 MB, a few MB is
		// all a console has to spare, so it doesn't get more than 2 pixels for a tile there. The images in view take at most
		// a half of it, the resolution is lowered otherwise, the rest is for those around it and those of another size.
		// Most chunks made in one frame and the time they may take, at least one is made, the others follow in the next
		// frames, so moving the view doesn't stall. A missing image is made with at most a few pixels for a tile first,
		// it's quick, so a layer is complete again soon, and made again with more of them later.
#if defined(DEATH_TARGET_CONSTRAINED_MEMORY)
		constexpr std::size_t MaxBytes = 4 * 1024 * 1024;
		constexpr std::size_t MaxPooledBytes = 1 * 1024 * 1024;
		constexpr std::int32_t MaxBuildsPerFrame = 2;
		constexpr float MaxBuildMilliseconds = 2.0f;
		constexpr float MaxMissingBuildMilliseconds = 4.0f;
#else
		// Phones, tablets and the Xbox have the memory a desktop has, but share it with much more
#	if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_IOS) || defined(DEATH_TARGET_WINDOWS_RT)
		constexpr std::size_t MaxBytes = 96 * 1024 * 1024;
		constexpr std::size_t MaxPooledBytes = 8 * 1024 * 1024;
#	else
		constexpr std::size_t MaxBytes = 384 * 1024 * 1024;
		constexpr std::size_t MaxPooledBytes = 32 * 1024 * 1024;
#	endif
		constexpr std::int32_t MaxBuildsPerFrame = 48;
		constexpr float MaxBuildMilliseconds = 4.0f;
		// The tiles of a chunk missing are drawn, which takes much longer from far away than making the chunk, so the
		// missing ones may take longer
		constexpr float MaxMissingBuildMilliseconds = 12.0f;
#endif
		constexpr std::size_t MaxBytesInView = MaxBytes / 2;
		// Frames after the layers were last drawn as images that the images are dropped, but for those made ahead, about
		// five seconds
		constexpr std::uint32_t EvictFrames = 300;
		// How much closer than MaxZoom the view can be for the colors of the tiles to be prepared, see WarmUp(), and how
		// long the level and the view have to stay the same for them to be prepared anyway, and for the changed images of
		// the layers made ahead to be made again
		constexpr float WarmUpZoomFactor = 4.0f;
		constexpr float IdleMilliseconds = 1000.0f;
		// Most pixels on a side of an image made quickly for a missing chunk
		constexpr std::int32_t FirstBuildPixels = 128;

		constexpr std::int32_t GetFirstBuildTilePixels(std::int32_t chunkTiles, std::int32_t tilePixels)
		{
			return std::min<std::int32_t>(tilePixels, std::max<std::int32_t>(FirstBuildPixels / chunkTiles, 1));
		}
		// Most images of a layer drawn in a frame, a layer that repeats a lot is cut off there, it takes a 4K screen at
		// the farthest zoom to get close
		constexpr std::int32_t MaxImagesPerLayer = 4096;

		constexpr std::size_t GetChunkBytes(std::int32_t tilePixels)
		{
			return std::size_t(TileLod::GetChunkTiles(tilePixels) * tilePixels) * std::size_t(TileLod::GetChunkTiles(tilePixels) * tilePixels) * 4;
		}

		// Combines the colors of 4 parts into one, weighted by how opaque they are, and as opaque as they are on average
		std::uint32_t CombineColors(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d)
		{
			std::uint32_t r = 0, g = 0, bl = 0, alpha = 0;
			for (std::uint32_t color : { a, b, c, d }) {
				std::uint32_t w = color >> 24;
				r += (color & 0xFF) * w;
				g += ((color >> 8) & 0xFF) * w;
				bl += ((color >> 16) & 0xFF) * w;
				alpha += w;
			}
			if (alpha == 0) {
				return 0;
			}
			return (r / alpha) | ((g / alpha) << 8) | ((bl / alpha) << 16) | ((alpha / 4) << 24);
		}

		using ToolMath::FloorDivide;
		using ToolMath::PositiveModulo;

		/*
			Where the chunks of a size are along one side of a layer. One that doesn't repeat is cut into chunks from its
			first tile on. One that repeats with a pattern at least as long as a chunk is cut the same way in each period
			of its pattern, the last chunk of a period ends with it and is drawn only up to there, so a chunk is the same
			image wherever the pattern repeats - a chunk starting anywhere in the pattern would be another image at every
			start. One with a shorter pattern has it a few times in a chunk, a power of two times, so the chunks of all
			sizes fit into each other as they do otherwise.
		*/
		struct ChunkSpan
		{
			// The first tile from the tile 0 of the layer without wrapping, the tiles shown, and the first tile in the layer
			std::int32_t World;
			std::int32_t Tiles;
			std::int32_t Start;
		};

		std::int32_t GetChunksPerPeriod(std::int32_t layerTiles, std::int32_t size)
		{
			return (layerTiles + size - 1) / size;
		}

		std::int32_t GetPeriodsPerChunk(std::int32_t layerTiles, std::int32_t size)
		{
			std::int32_t periods = 1;
			while (periods * 2 * layerTiles <= size) {
				periods *= 2;
			}
			return periods;
		}

		ChunkSpan GetChunkSpan(std::int32_t layerTiles, bool repeat, std::int32_t size, std::int32_t c)
		{
			if (!repeat) {
				return ChunkSpan{ c * size, size, c * size };
			}
			if (layerTiles >= size) {
				std::int32_t perPeriod = GetChunksPerPeriod(layerTiles, size);
				std::int32_t period = FloorDivide(c, perPeriod);
				std::int32_t index = c - period * perPeriod;
				return ChunkSpan{ period * layerTiles + index * size, std::min<std::int32_t>(size, layerTiles - index * size), index * size };
			}
			std::int32_t length = GetPeriodsPerChunk(layerTiles, size) * layerTiles;
			return ChunkSpan{ c * length, length, 0 };
		}

		// Returns the chunk of a size a tile is in, the tile counted from the tile 0 of the layer without wrapping
		std::int32_t GetChunkAt(std::int32_t layerTiles, bool repeat, std::int32_t size, std::int32_t tile)
		{
			if (!repeat) {
				return FloorDivide(tile, size);
			}
			if (layerTiles >= size) {
				std::int32_t period = FloorDivide(tile, layerTiles);
				return period * GetChunksPerPeriod(layerTiles, size) + (tile - period * layerTiles) / size;
			}
			return FloorDivide(tile, GetPeriodsPerChunk(layerTiles, size) * layerTiles);
		}

		// Returns the chunk of a larger size a chunk is in, the chunks of all sizes fit into each other
		std::int32_t GetLargerChunk(std::int32_t layerTiles, bool repeat, std::int32_t size, std::int32_t c, std::int32_t larger)
		{
			return GetChunkAt(layerTiles, repeat, larger, GetChunkSpan(layerTiles, repeat, size, c).World);
		}

		// Returns the first and the last chunk of a smaller size in a chunk, returns false if there's none, as past the end
		// of a layer that doesn't repeat
		bool GetSmallerChunks(std::int32_t layerTiles, bool repeat, std::int32_t size, std::int32_t c, std::int32_t smaller,
			std::int32_t& first, std::int32_t& last)
		{
			ChunkSpan span = GetChunkSpan(layerTiles, repeat, size, c);
			first = GetChunkAt(layerTiles, repeat, smaller, span.World);
			last = GetChunkAt(layerTiles, repeat, smaller, span.World + span.Tiles - 1);
			if (!repeat) {
				last = std::min<std::int32_t>(last, (layerTiles - 1) / smaller);
			}
			return (first <= last);
		}

		// Returns whether a chunk shows any tile of a part of a layer, both along one side, in the tiles of the layer
		bool ChunkOverlaps(std::int32_t start, std::int32_t size, std::int32_t areaStart, std::int32_t areaSize, std::int32_t layerTiles, bool repeat)
		{
			if (!repeat) {
				return (start < areaStart + areaSize && start + size > areaStart);
			}
			if (size >= layerTiles || areaSize >= layerTiles) {
				return true;
			}
			// Where the part starts from the start of the chunk, both wrap around the pattern
			std::int32_t offset = PositiveModulo(areaStart - start, layerTiles);
			return (offset < size || offset + areaSize > layerTiles);
		}
	}

	TileLod::TileLod(const LevelDocument& document, TileSetDataCache& tileSets)
		: _document(document), _tileSets(tileSets), _pooledBytes(0), _bytes(0), _frame(0), _buildStamp(0), _lastOverrideFrame(0), _evicted(true), _zoom(1.0f), _lastScale(0.0f),
			_buildsLeft(MaxBuildsPerFrame), _buildMilliseconds(0.0f), _tilePixels(TilePixels),
			_wantedTilePixels(TilePixels), _drawnChunks(0), _lastDrawnChunks(0), _drawnBytes(0), _lastDrawnBytes(0), _missingChunks(0), _lastMissingChunks(0), _drawnChunkTiles(GetChunkTiles(TilePixels)), _active(false),
			_warmUpState(WarmUpState::Done), _warmUpLayer(0), _warmUpRow(0), _warmUpColumn(0), _warmUpNext(0), _baseChunksDirty(false)
	{
		_lastChange = TimeStamp::now();
		ClearBlockCache();
		RestartWarmUp();
	}

	bool TileLod::IsWarmUpAllowed() const
	{
		// Close to where the images are drawn, as the view may get there fast, or when nothing changed for a while
		return (_zoom < MaxZoom * WarmUpZoomFactor || _lastChange.millisecondsSince() >= IdleMilliseconds);
	}

	void TileLod::RestartWarmUp()
	{
		_warmUpState = WarmUpState::Counting;
		_warmUpLayer = 0;
		_warmUpRow = 0;
		_warmUpColumn = 0;
		_warmUpCounts.clear();
		_warmUpKeys = SmallVector<std::uint32_t, 0>();
		_warmUpNext = 0;
	}

	void TileLod::WarmUp(float milliseconds)
	{
		if (!IsWarmUpAllowed()) {
			return;
		}
		if (_warmUpState == WarmUpState::Done) {
			// Those that changed are made again once the level isn't changed for a moment, they stand in for others only
			if (!_baseChunksDirty || !UseBaseChunks || _lastChange.millisecondsSince() < IdleMilliseconds) {
				return;
			}
			_baseChunksDirty = false;
			_warmUpState = WarmUpState::BaseChunks;
			_warmUpLayer = 0;
			_warmUpRow = 0;
			_warmUpColumn = 0;
		}

		TimeStamp start = TimeStamp::now();
		if (_warmUpState == WarmUpState::BaseChunks) {
			if (BuildBaseChunks(start, milliseconds)) {
				_warmUpState = WarmUpState::Done;
				LOGD("Images of the layers made ahead for views from far away, {:.1f} MB of images in all", double(_bytes) / (1024.0 * 1024.0));
			}
			return;
		}
		const LevelDocument& document = _document;
		while (_warmUpState == WarmUpState::Counting) {
			if (_warmUpLayer >= std::int32_t(document.Layers.size())) {
				// The most used first
				SmallVector<std::pair<std::uint32_t, std::uint32_t>, 0> byCount;
				byCount.reserve(_warmUpCounts.size());
				for (const auto& entry : _warmUpCounts) {
					byCount.push_back({ entry.second, entry.first });
				}
				std::sort(byCount.begin(), byCount.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
				_warmUpKeys.reserve(byCount.size());
				for (const auto& entry : byCount) {
					_warmUpKeys.push_back(entry.second);
				}
				_warmUpCounts.clear();
				_warmUpNext = 0;
				_warmUpState = WarmUpState::Computing;
				break;
			}
			const DocumentLayer& layer = document.Layers[_warmUpLayer];
			// A layer hidden in the editor isn't drawn, its tiles are prepared when they're seen
			if (_warmUpRow >= layer.GetHeight() || !RuntimeSync::IsLayerShown(layer) || !IsDrawnAsImages(layer)) {
				_warmUpLayer++;
				_warmUpRow = 0;
				continue;
			}
			// A row at a time, the same tile next to itself is counted once; the sprite layer is what's looked at, its
			// tiles count more
			std::uint32_t weight = (layer.IsSpriteLayer() ? 4 : 1);
			std::uint32_t runKey = 0, runLength = 0;
			std::int32_t width = layer.GetWidth();
			for (std::int32_t x = 0; x < width; x++) {
				const TileRef& tile = layer.GetTile(x, _warmUpRow);
				if (tile.IsEmpty() || (tile.Flags & Formats::TileRecord::ModifierMask) == Formats::TileRecord::ModifierInvisible) {
					continue;
				}
				std::uint32_t key = GetBlockKey(tile);
				if (runLength > 0 && key == runKey) {
					runLength++;
					continue;
				}
				if (runLength > 0) {
					_warmUpCounts[runKey] += runLength * weight;
				}
				runKey = key;
				runLength = 1;
			}
			if (runLength > 0) {
				_warmUpCounts[runKey] += runLength * weight;
			}
			_warmUpRow++;
			if (start.millisecondsSince() >= milliseconds) {
				return;
			}
		}

		while (_warmUpNext < _warmUpKeys.size()) {
			std::uint32_t key = _warmUpKeys[_warmUpNext++];
			GetBlock(TileRef{ std::uint16_t(key & 0xFFFFu), std::uint8_t(key >> 24), std::uint8_t((key >> 16) & 0x03) });
			if (start.millisecondsSince() >= milliseconds) {
				return;
			}
		}
		LOGD("Colors of {} tiles prepared for views from far away", std::int32_t(_warmUpKeys.size()));
		_warmUpKeys = SmallVector<std::uint32_t, 0>();
		if (UseBaseChunks) {
			_baseChunksDirty = false;
			_warmUpState = WarmUpState::BaseChunks;
			_warmUpLayer = 0;
			_warmUpRow = 0;
			_warmUpColumn = 0;
		} else {
			_warmUpState = WarmUpState::Done;
		}
	}

	bool TileLod::BuildBaseChunks(const TimeStamp& start, float milliseconds)
	{
		// Made outside of the frame budget, the images in view are made in it
		std::int32_t buildsLeft = _buildsLeft;
		float buildMilliseconds = _buildMilliseconds;
		const LevelDocument& document = _document;
		bool done = true;
		while (_warmUpLayer < std::int32_t(document.Layers.size())) {
			const DocumentLayer& layer = document.Layers[_warmUpLayer];
			std::int32_t rows = (layer.GetHeight() + MaxChunkTiles - 1) / MaxChunkTiles;
			std::int32_t columns = (layer.GetWidth() + MaxChunkTiles - 1) / MaxChunkTiles;
			// Those of a layer hidden in the editor are made once it's shown, see OnDocumentChanged(); the chunks start
			// where the pattern of a layer that repeats starts (see GetChunkSpan()), as those in view do
			if (!IsDrawnAsImages(layer) || !RuntimeSync::IsLayerShown(layer) || _warmUpRow >= rows) {
				_warmUpLayer++;
				_warmUpRow = 0;
				_warmUpColumn = 0;
				continue;
			}
			if (_warmUpColumn >= columns) {
				_warmUpRow++;
				_warmUpColumn = 0;
				continue;
			}
			Chunk& chunk = FindOrAddChunk(_warmUpLayer, MaxChunkTiles, Vector2i(_warmUpColumn * MaxChunkTiles, _warmUpRow * MaxChunkTiles), true);
			_warmUpColumn++;
			if (chunk.TilePixels == 0 || chunk.Dirty) {
				BuildChunk(chunk, 1);
				if (start.millisecondsSince() >= milliseconds) {
					done = false;
					break;
				}
			}
		}
		_buildsLeft = buildsLeft;
		_buildMilliseconds = buildMilliseconds;
		return done;
	}

	bool TileLod::IsDrawnAsImages(const DocumentLayer& layer)
	{
		auto rendererType = LayerRendererType(layer.Record.RendererType);
		// One through a palette of its own has its colors through it (see GetPaletteVariant())
		return (rendererType != LayerRendererType::Solid && rendererType != LayerRendererType::XRay && rendererType != LayerRendererType::BlurBehind &&
			layer.Record.Blend == std::uint8_t(Tiles::LayerBlend::Normal) && layer.GetWidth() > 0 && layer.GetHeight() > 0);
	}

	std::uint8_t TileLod::GetRepeats(const DocumentLayer& layer)
	{
		if (layer.IsSpriteLayer()) {
			return 0;
		}
		return std::uint8_t(((layer.Record.Flags & Formats::LayerRecord::RepeatX) != 0 ? 0x01 : 0) |
			((layer.Record.Flags & Formats::LayerRecord::RepeatY) != 0 ? 0x02 : 0));
	}

	TileLod::~TileLod()
	{
	}

	bool TileLod::UpdateActive(float zoom, float screenTilePixels)
	{
		float margin = (_active ? Hysteresis : 1.0f);
		_active = (zoom < MaxZoom * margin && screenTilePixels <= MaxScreenTilePixels * margin);
		_zoom = zoom;
		return _active;
	}

	void TileLod::Invalidate(bool keepBlocks)
	{
		ClearChunks();
		_lastChange = TimeStamp::now();
		if (!keepBlocks) {
			ClearBlockCache();
			_blocks.clear();
			_paletteBlocks.clear();
		}
		// Counted again, the colors kept are found quickly, then the images of the layers made ahead are made again
		RestartWarmUp();
	}

	void TileLod::ClearChunks()
	{
		for (Chunk& chunk : _chunks) {
			ReleaseTexture(Death::move(chunk.Image), chunk.LastUsed);
		}
		_chunks.clear();
		_chunkIndex.clear();
		_bytes = 0;
	}

	void TileLod::DropAnimatedBlocks()
	{
		ClearBlockCache();
		bool erased = false;
		for (auto it = _blocks.begin(); it != _blocks.end(); ) {
			if ((it->first >> 24) == TileRef::Animated) {
				it = _blocks.erase(it);
				erased = true;
			} else {
				++it;
			}
		}
		for (auto it = _paletteBlocks.begin(); it != _paletteBlocks.end(); ) {
			if (((it->first >> 24) & 0xFF) == TileRef::Animated) {
				it = _paletteBlocks.erase(it);
			} else {
				++it;
			}
		}
		if (erased) {
			RestartWarmUp();
		}
	}

	std::unique_ptr<Texture> TileLod::AcquireTexture(std::int32_t size)
	{
		// Not one drawn in this frame, the commands drawing it are still to come
		for (std::size_t i = _texturePool.size(); i > 0; i--) {
			PooledTexture& pooled = _texturePool[i - 1];
			if (pooled.Image->GetWidth() == size && pooled.LastDrawn != _frame) {
				std::unique_ptr<Texture> image = Death::move(pooled.Image);
				_pooledBytes -= std::size_t(size) * std::size_t(size) * 4;
				if (i < _texturePool.size()) {
					pooled = Death::move(_texturePool.back());
				}
				_texturePool.pop_back();
				return image;
			}
		}
		auto image = std::make_unique<Texture>("EditorLodChunk", Texture::Format::RGBA8, size, size);
		image->SetMinFiltering(SamplerFilter::Linear);
		image->SetMagFiltering(SamplerFilter::Linear);
		image->SetWrap(SamplerWrapping::ClampToEdge);
		return image;
	}

	void TileLod::ReleaseTexture(std::unique_ptr<Texture> image, std::uint32_t lastDrawn)
	{
		if (image == nullptr) {
			return;
		}
		// Only a few sizes are made (128, 256, 384 and 512 pixels on a side), so one is found for the next chunk soon;
		// those kept the longest go first when they'd take too much memory
		std::size_t bytes = std::size_t(image->GetWidth()) * std::size_t(image->GetHeight()) * 4;
		if (bytes > MaxPooledBytes) {
			return;
		}
		while (!_texturePool.empty() && _pooledBytes + bytes > MaxPooledBytes) {
			_pooledBytes -= std::size_t(_texturePool[0].Image->GetWidth()) * std::size_t(_texturePool[0].Image->GetHeight()) * 4;
			_texturePool.erase(_texturePool.begin());
		}
		_pooledBytes += bytes;
		_texturePool.push_back(PooledTexture{ Death::move(image), lastDrawn });
	}

	void TileLod::ClearBlockCache()
	{
		for (BlockCacheEntry& entry : _blockCache) {
			entry = BlockCacheEntry{ 0, nullptr };
		}
		_lastPaletteBlockKey = UINT64_MAX;
		_lastPaletteBlock = nullptr;
	}

	void TileLod::InvalidateTileIds(const LevelDocument& document, ArrayView<const std::uint16_t> sortedTileIds)
	{
		if (sortedTileIds.empty()) {
			return;
		}
		// The colors of the tiles, kept by how a tile is referenced, they're prepared again; every block a chunk shows is
		// there as long as the chunk isn't changed, so the keys of those dropped tell which chunks show the tiles
		_lastChange = TimeStamp::now();
		ClearBlockCache();
		SmallVector<std::uint32_t, 0> erased;
		for (auto it = _blocks.begin(); it != _blocks.end(); ) {
			std::uint32_t key = it->first;
			TileRef tile{ std::uint16_t(key & 0xFFFFu), std::uint8_t(key >> 24), std::uint8_t((key >> 16) & 0x03) };
			if (RuntimeSync::ShowsAnyTile(document, tile, sortedTileIds)) {
				it = _blocks.erase(it);
				erased.push_back(key);
			} else {
				++it;
			}
		}
		for (auto it = _paletteBlocks.begin(); it != _paletteBlocks.end(); ) {
			std::uint32_t key = std::uint32_t(it->first);
			TileRef tile{ std::uint16_t(key & 0xFFFFu), std::uint8_t(key >> 24), std::uint8_t((key >> 16) & 0x03) };
			if (RuntimeSync::ShowsAnyTile(document, tile, sortedTileIds)) {
				it = _paletteBlocks.erase(it);
				erased.push_back(key);
			} else {
				++it;
			}
		}
		if (erased.empty()) {
			return;
		}
		std::sort(erased.begin(), erased.end());
		// Prepared again first, without counting the tiles of the level again
		if (_warmUpState == WarmUpState::Computing) {
			for (std::uint32_t key : erased) {
				_warmUpKeys.push_back(key);
			}
		} else if (_warmUpState != WarmUpState::Counting) {
			_warmUpKeys = SmallVector<std::uint32_t, 0>();
			for (std::uint32_t key : erased) {
				_warmUpKeys.push_back(key);
			}
			_warmUpNext = 0;
			// Those of the layers made ahead too, all were made, only those that changed are made again
			_warmUpState = WarmUpState::Computing;
		}
		// The images that show them
		for (Chunk& chunk : _chunks) {
			if (chunk.Dirty) {
				continue;
			}
			auto key = chunk.Keys.begin();
			auto changed = erased.begin();
			while (key != chunk.Keys.end() && changed != erased.end()) {
				if (*key < *changed) {
					++key;
				} else if (*changed < *key) {
					++changed;
				} else {
					chunk.Dirty = true;
					_baseChunksDirty |= chunk.Base;
					break;
				}
			}
		}
	}

	void TileLod::OnDocumentChanged(const DocumentChanges& changes)
	{
		// A layer hidden or shown only isn't a change of the level
		if (changes.GetKinds() != DocumentChangeKind::EditorState) {
			_lastChange = TimeStamp::now();
		}
		// Other tile sets, the colors of the tiles may be other ones; overridden tiles are made again by InvalidateTileIds(),
		// only those that changed
		if (changes.HasAny(DocumentChangeKind::TileSets)) {
			Invalidate();
			return;
		}
		if (changes.HasAny(DocumentChangeKind::LayerStructure)) {
			ArrayView<const std::int32_t> order = changes.GetLayerOrder();
			if (order.empty()) {
				// Added, removed or resized, the colors of the tiles stay
				Invalidate(true);
			} else {
				// Only moved, the chunks are those of the same layers by other indices
				SmallVector<std::int32_t, 0> newIndices;
				newIndices.resize(order.size(), -1);
				for (std::size_t i = 0; i < order.size(); i++) {
					if (order[i] >= 0 && std::size_t(order[i]) < order.size()) {
						newIndices[order[i]] = std::int32_t(i);
					}
				}
				for (std::size_t i = 0; i < _chunks.size(); ) {
					Chunk& chunk = _chunks[i];
					std::int32_t layer = (chunk.Layer >= 0 && std::size_t(chunk.Layer) < newIndices.size() ? newIndices[chunk.Layer] : -1);
					if (layer < 0) {
						_bytes -= chunk.Bytes;
						ReleaseTexture(Death::move(chunk.Image), chunk.LastUsed);
						if (i + 1 < _chunks.size()) {
							chunk = Death::move(_chunks.back());
						}
						_chunks.pop_back();
						continue;
					}
					chunk.Layer = layer;
					i++;
				}
				_chunkIndex.clear();
				for (std::size_t i = 0; i < _chunks.size(); i++) {
					_chunkIndex.emplace(GetChunkKey(_chunks[i].Layer, _chunks[i].Size, _chunks[i].Start, _chunks[i].Base), std::int32_t(i));
				}
				// The warm-up goes through the layers by their indices
				if (_warmUpState != WarmUpState::Done) {
					RestartWarmUp();
				}
			}
		}
		if (changes.HasAny(DocumentChangeKind::AnimationStructure)) {
			// The animated tiles show other frames, the images are made again a few in a frame and shown as they were until then
			DropAnimatedBlocks();
			for (Chunk& chunk : _chunks) {
				chunk.Dirty = true;
			}
			_baseChunksDirty = true;
		}
		if (changes.HasAny(DocumentChangeKind::EditorState | DocumentChangeKind::LayerProperties)) {
			// A layer shown again, or drawn as images now, gets its images made ahead, those there already stay; those of a
			// layer that repeats otherwise now show other tiles past its end
			_baseChunksDirty = true;
			const LevelDocument& document = _document;
			for (Chunk& chunk : _chunks) {
				if (chunk.Layer >= 0 && chunk.Layer < std::int32_t(document.Layers.size()) && chunk.TilePixels != 0 &&
					(chunk.Repeats != GetRepeats(document.Layers[chunk.Layer]) || chunk.PaletteVariant != GetPaletteVariant(document.Layers[chunk.Layer]))) {
					chunk.Dirty = true;
				}
			}
		}
		if (changes.HasAny(DocumentChangeKind::Palette)) {
			// Only the colors change, the images are made again a few in a frame and shown as they were until then, so
			// a color dragged in the palette doesn't empty the view and make every image again at once for each step
			ClearBlockCache();
			_blocks.clear();
			_paletteBlocks.clear();
			RestartWarmUp();
			for (Chunk& chunk : _chunks) {
				chunk.Dirty = true;
			}
			_baseChunksDirty = true;
		}
		if (!changes.HasAny(DocumentChangeKind::Tiles)) {
			return;
		}

		const LevelDocument& document = _document;
		for (const DocumentTileArea& area : changes.GetTileAreas()) {
			if (area.Layer < 0 || area.Layer >= std::int32_t(document.Layers.size())) {
				continue;
			}
			const DocumentLayer& layer = document.Layers[area.Layer];
			std::uint8_t repeats = GetRepeats(layer);
			std::int32_t width = layer.GetWidth(), height = layer.GetHeight();
			const Recti& rect = area.Area;
			for (Chunk& chunk : _chunks) {
				if (chunk.Layer != area.Layer || chunk.Dirty) {
					continue;
				}
				// A chunk of a layer that repeats wraps around its pattern
				if (ChunkOverlaps(chunk.Start.X, chunk.Size, rect.X, rect.W, width, (repeats & 0x01) != 0) &&
					ChunkOverlaps(chunk.Start.Y, chunk.Size, rect.Y, rect.H, height, (repeats & 0x02) != 0)) {
					chunk.Dirty = true;
					_baseChunksDirty |= chunk.Base;
				}
			}
		}
	}

	void TileLod::BeginFrame(float scale)
	{
		_frame++;
		_buildsLeft = MaxBuildsPerFrame;
		_buildMilliseconds = 0.0f;
		_lastDrawnChunks = _drawnChunks;
		_lastDrawnBytes = _drawnBytes;
		_lastMissingChunks = _missingChunks;
		_drawnChunks = 0;
		_drawnBytes = 0;
		_missingChunks = 0;
		if (scale != _lastScale) {
			// The view zooms, so it isn't idle, see WarmUp()
			_lastScale = scale;
			_lastChange = TimeStamp::now();
		}
		if (!_evicted && _frame - _lastOverrideFrame > EvictFrames) {
			// The view is close for a while, so the images aren't drawn, but those made ahead are kept for when it gets far
			// again, with the colors of the tiles
			for (std::size_t i = 0; i < _chunks.size(); ) {
				if (!_chunks[i].Base) {
					_bytes -= _chunks[i].Bytes;
					if (i + 1 < _chunks.size()) {
						_chunks[i] = Death::move(_chunks.back());
					}
					_chunks.pop_back();
				} else {
					i++;
				}
			}
			_chunkIndex.clear();
			for (std::size_t i = 0; i < _chunks.size(); i++) {
				_chunkIndex.emplace(GetChunkKey(_chunks[i].Layer, _chunks[i].Size, _chunks[i].Start, _chunks[i].Base), std::int32_t(i));
			}
			_texturePool = SmallVector<PooledTexture, 0>();
			_pooledBytes = 0;
			_pixels = SmallVector<std::uint32_t, 0>();
			_evicted = true;
		}

		// At least as many pixels for a tile as a tile takes on the screen, so the images aren't blurred by being stretched,
		// and at most half again as many, more would be skipped by the filtering and flicker as the view moves, and take
		// memory for nothing; a chunk made with another number is shown until it's made again
		float screenPixels = scale * 32.0f;
		std::int32_t tilePixels = 1;
		while (tilePixels < TilePixels && float(tilePixels) < screenPixels) {
			tilePixels = GetLargerTileSize(tilePixels);
		}
		_wantedTilePixels = tilePixels;
		// Fewer, if the images in view wouldn't fit in the memory, as many as in the last frame for the size of their chunks
		auto chunksInView = [this](std::int32_t pixels) {
			float size = float(GetChunkTiles(pixels));
			float lastSize = float(_drawnChunkTiles);
			float ratio = ((_viewTiles.X / size + 1.0f) * (_viewTiles.Y / size + 1.0f)) / ((_viewTiles.X / lastSize + 1.0f) * (_viewTiles.Y / lastSize + 1.0f));
			return float(std::max<std::int32_t>(_lastDrawnChunks, 1)) * ratio;
		};
		while (tilePixels > 1 && chunksInView(tilePixels) * float(GetChunkBytes(tilePixels)) > float(MaxBytesInView)) {
			tilePixels = GetSmallerTileSize(tilePixels);
		}
		_tilePixels = tilePixels;
		_viewTiles = Vector2f();

		// Those drawn in the last frame are kept, the images are made in the frame
		DropOldChunks();
	}

	bool TileLod::OverrideLayer(std::int32_t layerIndex, Vector2f origin, const Rectf& cullingRect, SmallVectorImpl<TileMapLayerImage>& images,
		SmallVectorImpl<Recti>& tileAreas)
	{
		_lastOverrideFrame = _frame;
		_evicted = false;
		const LevelDocument& document = _document;
		if (layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size())) {
			return false;
		}
		const DocumentLayer& layer = document.Layers[layerIndex];
		std::int32_t width = layer.GetWidth(), height = layer.GetHeight();
		if (width <= 0 || height <= 0) {
			return true;
		}
		// Layers of a single color or of effects are drawn as they are, so is one with its colors moved through the palette
		if (!IsDrawnAsImages(layer)) {
			return false;
		}
		bool repeatX = !layer.IsSpriteLayer() && (layer.Record.Flags & Formats::LayerRecord::RepeatX) != 0;
		bool repeatY = !layer.IsSpriteLayer() && (layer.Record.Flags & Formats::LayerRecord::RepeatY) != 0;

		// Tiles in view, from the tile (0, 0) of the layer
		constexpr float TileSize = 32.0f;
		std::int32_t tx1 = std::int32_t(std::floor((cullingRect.X - origin.X) / TileSize));
		std::int32_t ty1 = std::int32_t(std::floor((cullingRect.Y - origin.Y) / TileSize));
		std::int32_t tx2 = std::int32_t(std::ceil((cullingRect.X + cullingRect.W - origin.X) / TileSize));
		std::int32_t ty2 = std::int32_t(std::ceil((cullingRect.Y + cullingRect.H - origin.Y) / TileSize));
		if (!repeatX) {
			tx1 = std::max<std::int32_t>(tx1, 0);
			tx2 = std::min<std::int32_t>(tx2, width);
		}
		if (!repeatY) {
			ty1 = std::max<std::int32_t>(ty1, 0);
			ty2 = std::min<std::int32_t>(ty2, height);
		}
		if (tx1 >= tx2 || ty1 >= ty2) {
			return true;
		}

		DrawnLayer drawnLayer{ layerIndex, width, height, repeatX, repeatY, origin };
		std::int32_t size = GetChunkTiles(_tilePixels);
		std::int32_t cx1 = GetChunkAt(width, repeatX, size, tx1), cx2 = GetChunkAt(width, repeatX, size, tx2 - 1);
		std::int32_t cy1 = GetChunkAt(height, repeatY, size, ty1), cy2 = GetChunkAt(height, repeatY, size, ty2 - 1);
		if (std::int64_t(cx2 - cx1 + 1) * std::int64_t(cy2 - cy1 + 1) > MaxImagesPerLayer) {
			// The images of a layer that repeats a lot would be cut off, the tiles are drawn instead
			return false;
		}
		_viewTiles.X = std::max(_viewTiles.X, float(tx2 - tx1));
		_viewTiles.Y = std::max(_viewTiles.Y, float(ty2 - ty1));
		_drawnChunkTiles = size;

		// What shows each chunk in view first, then the images, those that show others first
		std::size_t firstImage = images.size();
		std::size_t firstTileArea = tileAreas.size();
		_cells.clear();
		// A chunk of a layer that repeats is in view many times, its image takes the memory once
		auto countDrawn = [this](Chunk& chunk) {
			if (chunk.CountedFrame != _frame) {
				chunk.CountedFrame = _frame;
				_drawnChunks++;
				_drawnBytes += chunk.Bytes;
			}
		};
		for (std::int32_t cy = cy1; cy <= cy2; cy++) {
			for (std::int32_t cx = cx1; cx <= cx2; cx++) {
				Chunk& chunk = FindOrAddChunk(layerIndex, size, GetChunkStart(drawnLayer, size, cx, cy));
				ViewCell& cell = _cells.emplace_back(ViewCell{ cx, cy, Cover() });
				bool canBuild = (_buildsLeft > 0 && (_buildMilliseconds < MaxBuildMilliseconds || _buildsLeft == MaxBuildsPerFrame));
				if (chunk.TilePixels == 0) {
					cell.By = FindCover(drawnLayer, size, cx, cy);
					chunk.LastUsed = _frame;
					if (cell.By.Kind != CoverKind::None) {
						// Shown by chunks of another size made before, as when the view got closer or farther, it's made
						// with all of its pixels once nothing is missing, and drawn itself from the next frame
						countDrawn(chunk);
						if (canBuild && _lastMissingChunks == 0) {
							BuildChunk(chunk, _tilePixels);
						}
						continue;
					}
					// Missing, quickly with a few pixels for a tile, it's made again with more of them later
					if (_buildsLeft <= 0 || (_buildMilliseconds >= MaxMissingBuildMilliseconds && _buildsLeft != MaxBuildsPerFrame)) {
						// Not in this frame, the image of the layer made ahead shows it then, however blurred, or its tiles
						// are drawn, which takes much longer
						cell.By = FindBaseCover(drawnLayer, size, cx, cy);
						if (cell.By.Kind == CoverKind::None) {
							cell.By.Kind = CoverKind::Tiles;
						}
						countDrawn(chunk);
						_missingChunks++;
						continue;
					}
					BuildChunk(chunk, GetFirstBuildTilePixels(size, _tilePixels));
				} else if ((chunk.Dirty || chunk.TilePixels != _tilePixels) && canBuild && _lastMissingChunks == 0 && chunk.LastUsed != _frame) {
					// Changed, or of another resolution, once nothing is missing anymore; the image replaced isn't one
					// already drawn in this frame
					BuildChunk(chunk, _tilePixels);
				}
				if (chunk.Image != nullptr) {
					countDrawn(chunk);
				}
			}
		}

		// A larger chunk or the image of the layer shows the chunks around the one not made yet too, so they aren't drawn
		// over each other
		_coverRects.clear();
		for (const ViewCell& cell : _cells) {
			if (cell.By.Kind == CoverKind::Larger || cell.By.Kind == CoverKind::Base) {
				AddChunkImage(drawnLayer, cell.By.Size, cell.By.X, cell.By.Y, images, firstImage, cell.By.Kind == CoverKind::Base);
				_coverRects.push_back(GetChunkRect(drawnLayer, cell.By.Size, cell.By.X, cell.By.Y));
			}
		}
		auto isCovered = [this](const Rectf& rect) {
			for (const Rectf& cover : _coverRects) {
				if (rect.X >= cover.X && rect.Y >= cover.Y && rect.X + rect.W <= cover.X + cover.W && rect.Y + rect.H <= cover.Y + cover.H) {
					return true;
				}
			}
			return false;
		};
		for (const ViewCell& cell : _cells) {
			if (cell.By.Kind == CoverKind::Smaller) {
				std::int32_t smaller = cell.By.Size;
				std::int32_t x1, x2, y1, y2;
				if (!isCovered(GetChunkRect(drawnLayer, size, cell.X, cell.Y)) &&
					GetSmallerChunks(width, repeatX, size, cell.X, smaller, x1, x2) && GetSmallerChunks(height, repeatY, size, cell.Y, smaller, y1, y2)) {
					for (std::int32_t y = y1; y <= y2; y++) {
						for (std::int32_t x = x1; x <= x2; x++) {
							AddChunkImage(drawnLayer, smaller, x, y, images, firstImage);
						}
					}
				}
			} else if (cell.By.Kind == CoverKind::None) {
				if (!isCovered(GetChunkRect(drawnLayer, size, cell.X, cell.Y))) {
					AddChunkImage(drawnLayer, size, cell.X, cell.Y, images, firstImage);
				}
			} else if (cell.By.Kind == CoverKind::Tiles) {
				// Its tiles, joined with those of the chunk before it in the row
				if (!isCovered(GetChunkRect(drawnLayer, size, cell.X, cell.Y))) {
					Recti area = GetChunkTileArea(drawnLayer, size, cell.X, cell.Y);
					if (tileAreas.size() > firstTileArea) {
						Recti& last = tileAreas.back();
						if (last.Y == area.Y && last.H == area.H && last.X + last.W == area.X) {
							last.W += area.W;
							continue;
						}
					}
					tileAreas.push_back(area);
				}
			}
		}

		// The chunks around the view are made before they come into it, so the layer isn't drawn as tiles while the view
		// moves; they count as missing, so they're made before the resolution of others is changed
		for (std::int32_t cy = cy1 - 1; cy <= cy2 + 1; cy++) {
			if (!repeatY && (cy < 0 || cy * size >= height)) {
				continue;
			}
			for (std::int32_t cx = cx1 - 1; cx <= cx2 + 1; cx++) {
				if ((cy >= cy1 && cy <= cy2 && cx >= cx1 && cx <= cx2) || (!repeatX && (cx < 0 || cx * size >= width))) {
					continue;
				}
				Vector2i start = GetChunkStart(drawnLayer, size, cx, cy);
				Chunk* chunk = FindChunk(layerIndex, size, start);
				if (chunk != nullptr && chunk->TilePixels != 0) {
					chunk->LastUsed = _frame;
					continue;
				}
				if (_buildsLeft <= 0 || _buildMilliseconds >= MaxBuildMilliseconds) {
					_missingChunks++;
					continue;
				}
				Chunk& added = FindOrAddChunk(layerIndex, size, start);
				BuildChunk(added, GetFirstBuildTilePixels(size, _tilePixels));
				added.LastUsed = _frame;
			}
		}
		return true;
	}

	std::uint64_t TileLod::GetChunkKey(std::int32_t layer, std::int32_t size, Vector2i start, bool base)
	{
		// The images of a layer made ahead have the size of the largest chunks, they're told apart
		return (std::uint64_t(layer & 0xFFFF) << 48) | (std::uint64_t(base ? 0xFF : (size & 0x7F)) << 40) |
			(std::uint64_t(start.X & 0xFFFFF) << 20) | std::uint64_t(start.Y & 0xFFFFF);
	}

	TileLod::Chunk* TileLod::FindChunk(std::int32_t layer, std::int32_t size, Vector2i start, bool base)
	{
		auto it = _chunkIndex.find(GetChunkKey(layer, size, start, base));
		return (it != _chunkIndex.end() ? &_chunks[it->second] : nullptr);
	}

	TileLod::Chunk& TileLod::FindOrAddChunk(std::int32_t layer, std::int32_t size, Vector2i start, bool base)
	{
		if (Chunk* chunk = FindChunk(layer, size, start, base)) {
			return *chunk;
		}
		_chunkIndex.emplace(GetChunkKey(layer, size, start, base), std::int32_t(_chunks.size()));
		Chunk& chunk = _chunks.emplace_back();
		chunk.Layer = layer;
		chunk.Size = size;
		chunk.Start = start;
		chunk.LastUsed = _frame;
		chunk.Dirty = true;
		chunk.Base = base;
		return chunk;
	}

	Vector2i TileLod::GetChunkStart(const DrawnLayer& layer, std::int32_t size, std::int32_t cx, std::int32_t cy)
	{
		// Where the pattern of a layer that repeats is at the start of the chunk, the image is the same there
		return Vector2i(GetChunkSpan(layer.Width, layer.RepeatX, size, cx).Start, GetChunkSpan(layer.Height, layer.RepeatY, size, cy).Start);
	}

	Rectf TileLod::GetChunkRect(const DrawnLayer& layer, std::int32_t size, std::int32_t cx, std::int32_t cy, Vector2f* texScale)
	{
		constexpr float TileSize = 32.0f;
		ChunkSpan spanX = GetChunkSpan(layer.Width, layer.RepeatX, size, cx);
		ChunkSpan spanY = GetChunkSpan(layer.Height, layer.RepeatY, size, cy);
		if (texScale != nullptr) {
			*texScale = Vector2f(float(spanX.Tiles) / float(size), float(spanY.Tiles) / float(size));
		}
		return Rectf(layer.Origin.X + float(spanX.World) * TileSize, layer.Origin.Y + float(spanY.World) * TileSize,
			float(spanX.Tiles) * TileSize, float(spanY.Tiles) * TileSize);
	}

	Recti TileLod::GetChunkTileArea(const DrawnLayer& layer, std::int32_t size, std::int32_t cx, std::int32_t cy)
	{
		ChunkSpan spanX = GetChunkSpan(layer.Width, layer.RepeatX, size, cx);
		ChunkSpan spanY = GetChunkSpan(layer.Height, layer.RepeatY, size, cy);
		return Recti(spanX.World, spanY.World, spanX.Tiles, spanY.Tiles);
	}

	bool TileLod::AddChunkImage(const DrawnLayer& layer, std::int32_t size, std::int32_t cx, std::int32_t cy, SmallVectorImpl<TileMapLayerImage>& images,
		std::size_t firstImage, bool base)
	{
		Chunk* chunk = FindChunk(layer.Index, size, GetChunkStart(layer, size, cx, cy), base);
		if (chunk == nullptr || chunk->TilePixels == 0) {
			return false;
		}
		chunk->LastUsed = _frame;
		if (chunk->Image == nullptr) {
			// Nothing to see in it
			return true;
		}
		Vector2f texScale;
		Rectf rect = GetChunkRect(layer, size, cx, cy, &texScale);
		// A larger chunk shows the place of several chunks not made yet, it's drawn once
		for (std::size_t i = firstImage; i < images.size(); i++) {
			if (images[i].Rect.X == rect.X && images[i].Rect.Y == rect.Y && images[i].Rect.W == rect.W) {
				return true;
			}
		}
		images.push_back(TileMapLayerImage{ chunk->Image.get(), rect, texScale });
		return true;
	}

	TileLod::Cover TileLod::FindCover(const DrawnLayer& layer, std::int32_t size, std::int32_t cx, std::int32_t cy)
	{
		// A larger chunk around it, as when the view got closer
		for (std::int32_t larger = size * 2; larger <= MaxChunkTiles; larger *= 2) {
			std::int32_t x = GetLargerChunk(layer.Width, layer.RepeatX, size, cx, larger);
			std::int32_t y = GetLargerChunk(layer.Height, layer.RepeatY, size, cy, larger);
			Chunk* chunk = FindChunk(layer.Index, larger, GetChunkStart(layer, larger, x, y));
			if (chunk != nullptr && chunk->TilePixels != 0) {
				return Cover{ CoverKind::Larger, larger, x, y };
			}
		}
		// Or all the smaller ones in it, as when the view got farther, those past the end of a layer that doesn't repeat
		// aren't needed
		for (std::int32_t smaller = size / 2; smaller >= MinChunkTiles; smaller /= 2) {
			std::int32_t x1, x2, y1, y2;
			if (!GetSmallerChunks(layer.Width, layer.RepeatX, size, cx, smaller, x1, x2) ||
				!GetSmallerChunks(layer.Height, layer.RepeatY, size, cy, smaller, y1, y2)) {
				continue;
			}
			bool all = true;
			for (std::int32_t y = y1; y <= y2 && all; y++) {
				for (std::int32_t x = x1; x <= x2 && all; x++) {
					Chunk* chunk = FindChunk(layer.Index, smaller, GetChunkStart(layer, smaller, x, y));
					all = (chunk != nullptr && chunk->TilePixels != 0);
				}
			}
			if (all) {
				return Cover{ CoverKind::Smaller, smaller, cx, cy };
			}
		}
		// Or the image of the layer made ahead, only from far away, it's blurred
		if (_tilePixels <= MaxBaseCoverTilePixels) {
			return FindBaseCover(layer, size, cx, cy);
		}
		return Cover();
	}

	TileLod::Cover TileLod::FindBaseCover(const DrawnLayer& layer, std::int32_t size, std::int32_t cx, std::int32_t cy)
	{
		if (!UseBaseChunks) {
			return Cover();
		}
		// Of the size of the largest chunks, those start where the others do
		std::int32_t x = GetLargerChunk(layer.Width, layer.RepeatX, size, cx, MaxChunkTiles);
		std::int32_t y = GetLargerChunk(layer.Height, layer.RepeatY, size, cy, MaxChunkTiles);
		Chunk* chunk = FindChunk(layer.Index, MaxChunkTiles, GetChunkStart(layer, MaxChunkTiles, x, y), true);
		if (chunk != nullptr && chunk->TilePixels != 0) {
			return Cover{ CoverKind::Base, MaxChunkTiles, x, y };
		}
		return Cover();
	}

	void TileLod::BuildChunk(Chunk& chunk, std::int32_t tilePixels)
	{
		TimeStamp start = TimeStamp::now();
		_buildsLeft--;
		_bytes -= chunk.Bytes;
		chunk.Dirty = false;
		chunk.TilePixels = tilePixels;
		chunk.Bytes = 0;
		chunk.Keys.clear();
		const LevelDocument& document = _document;
		if (chunk.Layer >= std::int32_t(document.Layers.size())) {
			ReleaseTexture(Death::move(chunk.Image), chunk.LastUsed);
			return;
		}
		const DocumentLayer& layer = document.Layers[chunk.Layer];
		std::int32_t width = layer.GetWidth(), height = layer.GetHeight();
		chunk.Repeats = GetRepeats(layer);
		chunk.PaletteVariant = GetPaletteVariant(layer);
		bool repeatX = (chunk.Repeats & 0x01) != 0;
		bool repeatY = (chunk.Repeats & 0x02) != 0;

		// The colors of a tile of the size are copied row by row, only a translucent tile is changed on the way, by the alpha
		// the game draws it with; all the smaller sizes are before them in a block
		constexpr std::uint32_t TranslucentAlpha = Tiles::TileMap::TranslucentTileAlpha;
		std::int32_t size = chunk.Size * tilePixels;
		std::int32_t levelOffset = Implementation::GetLodLevelOffset(tilePixels);
		_pixels.clear();
		_pixels.resize(std::size_t(size) * std::size_t(size), 0);
		bool empty = true;
		// Each block it shows once in its keys
		_buildStamp++;
		for (std::int32_t y = 0; y < chunk.Size; y++) {
			std::int32_t ty = chunk.Start.Y + y;
			if (repeatY) {
				ty = PositiveModulo(ty, height);
			} else if (ty >= height) {
				break;
			}
			for (std::int32_t x = 0; x < chunk.Size; x++) {
				std::int32_t tx = chunk.Start.X + x;
				if (repeatX) {
					tx = PositiveModulo(tx, width);
				} else if (tx >= width) {
					break;
				}
				const TileRef& tile = layer.GetTile(tx, ty);
				std::uint8_t modifier = (tile.Flags & Formats::TileRecord::ModifierMask);
				if (tile.IsEmpty() || modifier == Formats::TileRecord::ModifierInvisible) {
					continue;
				}
				TileBlock& block = (chunk.PaletteVariant != 0 ? GetPaletteBlock(tile, chunk.PaletteVariant) : GetBlock(tile));
				// Even one that shows nothing yet, its tile can be drawn on
				if (block.BuildStamp != _buildStamp) {
					block.BuildStamp = _buildStamp;
					chunk.Keys.push_back(GetBlockKey(tile));
				}
				if (!block.Visible) {
					continue;
				}
				empty = false;
				const std::uint32_t* colors = block.Colors + levelOffset;
				bool translucent = (modifier == Formats::TileRecord::ModifierTranslucent);
				for (std::int32_t j = 0; j < tilePixels; j++) {
					std::uint32_t* row = &_pixels[std::size_t(y * tilePixels + j) * std::size_t(size) + std::size_t(x * tilePixels)];
					const std::uint32_t* source = colors + j * tilePixels;
					if (!translucent) {
						std::memcpy(row, source, std::size_t(tilePixels) * sizeof(std::uint32_t));
						continue;
					}
					for (std::int32_t i = 0; i < tilePixels; i++) {
						std::uint32_t color = source[i];
						row[i] = (color & 0x00FFFFFFu) | (((color >> 24) * TranslucentAlpha / 255) << 24);
					}
				}
			}
		}
		std::sort(chunk.Keys.begin(), chunk.Keys.end());

		if (empty) {
			ReleaseTexture(Death::move(chunk.Image), chunk.LastUsed);
		} else {
			// A transparent texel takes the color of the visible ones next to it, it's filtered with them where the image is
			// drawn larger or smaller than it is, and its black would darken their edges otherwise
			std::uint32_t* pixels = _pixels.data();
			for (std::int32_t y = 0; y < size; y++) {
				std::uint32_t* row = pixels + std::size_t(y) * std::size_t(size);
				const std::uint32_t* above = (y > 0 ? row - size : nullptr);
				const std::uint32_t* below = (y + 1 < size ? row + size : nullptr);
				for (std::int32_t x = 0; x < size; x++) {
					if ((row[x] >> 24) != 0) {
						continue;
					}
					std::uint32_t r = 0, g = 0, b = 0, n = 0;
					auto add = [&](std::uint32_t color) {
						if ((color >> 24) != 0) {
							r += (color & 0xFF);
							g += ((color >> 8) & 0xFF);
							b += ((color >> 16) & 0xFF);
							n++;
						}
					};
					if (x > 0) {
						add(row[x - 1]);
					}
					if (x + 1 < size) {
						add(row[x + 1]);
					}
					if (above != nullptr) {
						add(above[x]);
					}
					if (below != nullptr) {
						add(below[x]);
					}
					if (n > 0) {
						row[x] = (r / n) | ((g / n) << 8) | ((b / n) << 16);
					}
				}
			}

			if (chunk.Image == nullptr || chunk.Image->GetWidth() != size) {
				ReleaseTexture(Death::move(chunk.Image), chunk.LastUsed);
				chunk.Image = AcquireTexture(size);
			}
			chunk.Image->LoadFromTexels(reinterpret_cast<const std::uint8_t*>(_pixels.data()), 0, 0, size, size);
			chunk.Bytes = std::size_t(size) * std::size_t(size) * 4;
		}
		_bytes += chunk.Bytes;
		_buildMilliseconds += start.millisecondsSince();
	}

	std::uint32_t TileLod::GetBlockKey(const TileRef& tile)
	{
		return (std::uint32_t(tile.Source) << 24) | (std::uint32_t(tile.Flags & 0x03) << 16) | tile.Index;
	}

	TileLod::TileBlock& TileLod::GetBlock(const TileRef& tile)
	{
		// Flips change the block, the rest of the flags don't
		std::uint32_t key = GetBlockKey(tile);
		BlockCacheEntry& cached = _blockCache[(key ^ (key >> 10) ^ (key >> 24)) & std::uint32_t(BlockCacheSize - 1)];
		if (cached.Block != nullptr && cached.Key == key) {
			return *cached.Block;
		}
		auto it = _blocks.find(key);
		if (it != _blocks.end()) {
			cached = BlockCacheEntry{ key, &it->second };
			return it->second;
		}

		// The blocks stay where they are in the map when another one is added, so the cache stays valid
		TileBlock& block = _blocks[key];
		cached = BlockCacheEntry{ key, &block };
		FillBlock(block, tile, ContentResolver::Get().GetPalettes().data());
		return block;
	}

	TileLod::TileBlock& TileLod::GetPaletteBlock(const TileRef& tile, std::uint32_t variant)
	{
		std::uint64_t key = (std::uint64_t(variant) << 32) | GetBlockKey(tile);
		if (key == _lastPaletteBlockKey && _lastPaletteBlock != nullptr) {
			return *_lastPaletteBlock;
		}
		auto it = _paletteBlocks.find(key);
		if (it == _paletteBlocks.end()) {
			// The colors of the row the layer is drawn through (see TileMap::AcquireLayerPalette()), without taking one
			std::uint32_t palette[Formats::LevelFile::ColorsPerPalette];
			auto rendererType = LayerRendererType(variant >> 8);
			std::uint8_t param = std::uint8_t(variant & 0xFF);
			if (rendererType == LayerRendererType::PaletteMapping) {
				if (param < _document.AdditionalPalettes.size()) {
					std::memcpy(palette, _document.AdditionalPalettes[param].Colors, sizeof(palette));
				} else {
					std::memcpy(palette, ContentResolver::Get().GetPalettes().data(), sizeof(palette));
				}
			} else {
				ContentResolver::Get().BuildLayerPalette(rendererType == LayerRendererType::AlphaMap, param, palette);
			}
			it = _paletteBlocks.emplace(key, TileBlock{}).first;
			FillBlock(it->second, tile, palette);
		}
		_lastPaletteBlockKey = key;
		_lastPaletteBlock = &it->second;
		return it->second;
	}

	std::uint32_t TileLod::GetPaletteVariant(const DocumentLayer& layer)
	{
		auto rendererType = LayerRendererType(layer.Record.RendererType);
		if ((rendererType != LayerRendererType::PaletteShift && rendererType != LayerRendererType::AlphaMap &&
			 rendererType != LayerRendererType::PaletteMapping) || (rendererType == LayerRendererType::PaletteShift && layer.Record.Color[0] == 0)) {
			return 0;
		}
		return (std::uint32_t(layer.Record.RendererType) << 8) | layer.Record.Color[0];
	}

	bool TileLod::FillBlock(TileBlock& block, const TileRef& tile, const std::uint32_t* palette)
	{
		std::memset(block.Colors, 0, sizeof(block.Colors));
		block.Visible = false;
		LevelThumbnail::TilePixels source;
		TileRef plain = tile;
		plain.Flags &= 0x03;
		if (!LevelThumbnail::GetTilePixels(_document, _tileSets, plain, source)) {
			return false;
		}

		// The pixels as the tile shows them, read once for all sizes
		std::uint32_t pixels[32 * 32];
		for (std::int32_t y = 0; y < 32; y++) {
			for (std::int32_t x = 0; x < 32; x++) {
				pixels[y * 32 + x] = source.GetPixel(x, y, palette);
			}
		}

		// The largest size first, each part the average of its pixels, weighted by how opaque they are, and as opaque as
		// they are on average
		constexpr std::int32_t Part = 32 / TilePixels;
		std::uint32_t* largest = block.Colors + Implementation::GetLodLevelOffset(TilePixels);
		for (std::int32_t j = 0; j < TilePixels; j++) {
			for (std::int32_t i = 0; i < TilePixels; i++) {
				std::uint32_t r = 0, g = 0, b = 0, a = 0;
				for (std::int32_t y = 0; y < Part; y++) {
					for (std::int32_t x = 0; x < Part; x++) {
						std::uint32_t color = pixels[(j * Part + y) * 32 + i * Part + x];
						std::uint32_t alpha = color >> 24;
						r += (color & 0xFF) * alpha;
						g += ((color >> 8) & 0xFF) * alpha;
						b += ((color >> 16) & 0xFF) * alpha;
						a += alpha;
					}
				}
				if (a > 0) {
					largest[j * TilePixels + i] = (r / a) | ((g / a) << 8) | ((b / a) << 16) | ((a / (Part * Part)) << 24);
					block.Visible = true;
				}
			}
		}

		// Each smaller size from the one twice as large, 4 parts into one, the same as from the pixels but for rounding; a
		// size half again as large as a power of two that has none from the pixels, its parts cover parts of them
		for (std::int32_t size = GetSmallerTileSize(TilePixels); size >= 1; size = (size > 1 ? GetSmallerTileSize(size) : 0)) {
			std::uint32_t* level = block.Colors + Implementation::GetLodLevelOffset(size);
			if (size * 2 <= TilePixels) {
				const std::uint32_t* larger = block.Colors + Implementation::GetLodLevelOffset(size * 2);
				std::int32_t largerSize = size * 2;
				for (std::int32_t j = 0; j < size; j++) {
					for (std::int32_t i = 0; i < size; i++) {
						const std::uint32_t* p = larger + (j * 2) * largerSize + i * 2;
						level[j * size + i] = CombineColors(p[0], p[1], p[largerSize], p[largerSize + 1]);
					}
				}
				continue;
			}
			float step = 32.0f / float(size);
			for (std::int32_t j = 0; j < size; j++) {
				float y0 = float(j) * step, y1 = y0 + step;
				for (std::int32_t i = 0; i < size; i++) {
					float x0 = float(i) * step, x1 = x0 + step;
					float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
					for (std::int32_t y = std::int32_t(y0); y < 32 && float(y) < y1; y++) {
						float coverY = std::min(float(y + 1), y1) - std::max(float(y), y0);
						for (std::int32_t x = std::int32_t(x0); x < 32 && float(x) < x1; x++) {
							float cover = coverY * (std::min(float(x + 1), x1) - std::max(float(x), x0));
							std::uint32_t color = pixels[y * 32 + x];
							float alpha = float(color >> 24) * cover;
							r += float(color & 0xFF) * alpha;
							g += float((color >> 8) & 0xFF) * alpha;
							b += float((color >> 16) & 0xFF) * alpha;
							a += alpha;
						}
					}
					if (a > 0.0f) {
						level[j * size + i] = std::uint32_t(r / a + 0.5f) | (std::uint32_t(g / a + 0.5f) << 8) | (std::uint32_t(b / a + 0.5f) << 16) |
							(std::min<std::uint32_t>(std::uint32_t(a / (step * step) + 0.5f), 255) << 24);
					}
				}
			}
		}
		return true;
	}

	void TileLod::DropOldChunks()
	{
		// Chunks not made yet that weren't in view in the last frame go anyway, they take no memory, but would pile up
		bool changed = false;
		for (std::size_t i = 0; i < _chunks.size(); ) {
			if (_chunks[i].TilePixels == 0 && _chunks[i].LastUsed + 1 < _frame && !_chunks[i].Base) {
				if (i + 1 < _chunks.size()) {
					_chunks[i] = Death::move(_chunks.back());
				}
				_chunks.pop_back();
				changed = true;
			} else {
				i++;
			}
		}
		if (_bytes > MaxBytes) {
			// The images of the layers made ahead stay, so do those drawn in the last frame, even if there are more of them,
			// and the most recently drawn of the others
			std::sort(_chunks.begin(), _chunks.end(), [](const Chunk& a, const Chunk& b) {
				return (a.Base != b.Base ? a.Base : a.LastUsed > b.LastUsed);
			});
			std::size_t keep = 0;
			std::size_t bytes = 0;
			while (keep < _chunks.size() && (_chunks[keep].Base || _chunks[keep].LastUsed + 1 >= _frame || bytes + _chunks[keep].Bytes <= MaxBytes)) {
				bytes += _chunks[keep].Bytes;
				keep++;
			}
			// Their images are made again for other chunks of the same size soon
			for (std::size_t i = keep; i < _chunks.size(); i++) {
				ReleaseTexture(Death::move(_chunks[i].Image), _chunks[i].LastUsed);
			}
			_chunks.erase(_chunks.begin() + keep, _chunks.end());
			_bytes = bytes;
			changed = true;
		}
		if (changed) {
			_chunkIndex.clear();
			for (std::size_t i = 0; i < _chunks.size(); i++) {
				_chunkIndex.emplace(GetChunkKey(_chunks[i].Layer, _chunks[i].Size, _chunks[i].Start, _chunks[i].Base), std::int32_t(i));
			}
		}
	}
}

#endif
