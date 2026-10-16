#include "TileLod.h"

#if defined(WITH_EDITOR)

#include "../EditorSession.h"
#include "../Document/DocumentChanges.h"
#include "../Imaging/LevelThumbnail.h"
#include "../../ContentResolver.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Jazz2::Tiles;
using namespace nCine;

namespace Jazz2::Editor
{
	namespace
	{
		// Most chunks kept, those drawn the longest time ago go first; an image is 256 KB, a few MB is all a console has
		// to spare. Most chunks made in one frame, the others follow in the next frames, so moving the view doesn't stall.
#if defined(DEATH_TARGET_CONSTRAINED_MEMORY)
		constexpr std::size_t MaxChunks = 24;
		constexpr std::int32_t MaxBuildsPerFrame = 2;
#else
		constexpr std::size_t MaxChunks = 160;
		constexpr std::int32_t MaxBuildsPerFrame = 8;
#endif
		// Most images of a layer drawn in a frame, a layer that repeats a lot is cut off there
		constexpr std::int32_t MaxImagesPerLayer = 1024;

		std::int32_t FloorDiv(std::int32_t value, std::int32_t divisor)
		{
			return (value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor));
		}

		std::int32_t Mod(std::int32_t value, std::int32_t divisor)
		{
			std::int32_t result = value % divisor;
			return (result < 0 ? result + divisor : result);
		}
	}

	TileLod::TileLod(EditorSession& session)
		: _session(session), _frame(0), _buildsLeft(MaxBuildsPerFrame)
	{
	}

	TileLod::~TileLod()
	{
	}

	void TileLod::Invalidate()
	{
		_chunks.clear();
		_blocks.clear();
	}

	void TileLod::OnDocumentChanged(const DocumentChanges& changes)
	{
		constexpr DocumentChangeKind AllKinds = DocumentChangeKind::LayerStructure | DocumentChangeKind::TileSets |
			DocumentChangeKind::TileOverrides | DocumentChangeKind::Palette | DocumentChangeKind::AnimationStructure;
		if (changes.HasAny(AllKinds)) {
			Invalidate();
			return;
		}
		if (!changes.HasAny(DocumentChangeKind::Tiles)) {
			return;
		}

		const LevelDocument& document = _session.GetDocument();
		for (const DocumentTileArea& area : changes.GetTileAreas()) {
			if (area.Layer < 0 || area.Layer >= std::int32_t(document.Layers.size())) {
				continue;
			}
			const DocumentLayer& layer = document.Layers[area.Layer];
			bool repeats = !layer.IsSpriteLayer() && (layer.Record.Flags & (Formats::LayerRecord::RepeatX | Formats::LayerRecord::RepeatY)) != 0;
			for (Chunk& chunk : _chunks) {
				if (chunk.Layer != area.Layer) {
					continue;
				}
				// A chunk of a layer that repeats can start anywhere in its pattern and wrap around, so all of them
				const Recti& rect = area.Area;
				if (repeats || (chunk.Start.X < rect.X + rect.W && chunk.Start.X + ChunkTiles > rect.X &&
					chunk.Start.Y < rect.Y + rect.H && chunk.Start.Y + ChunkTiles > rect.Y)) {
					chunk.Dirty = true;
				}
			}
		}
	}

	void TileLod::BeginFrame()
	{
		_frame++;
		_buildsLeft = MaxBuildsPerFrame;
	}

	bool TileLod::OverrideLayer(std::int32_t layerIndex, Vector2f origin, const Rectf& cullingRect, SmallVectorImpl<TileMapLayerImage>& images)
	{
		const LevelDocument& document = _session.GetDocument();
		if (layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size())) {
			return false;
		}
		const DocumentLayer& layer = document.Layers[layerIndex];
		auto rendererType = LayerRendererType(layer.Record.RendererType);
		// Layers of a single color or of effects are drawn as they are
		if (rendererType == LayerRendererType::Solid || rendererType == LayerRendererType::XRay || rendererType == LayerRendererType::BlurBehind) {
			return false;
		}
		std::int32_t width = layer.GetWidth(), height = layer.GetHeight();
		if (width <= 0 || height <= 0) {
			return true;
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
			tx1 = std::max(tx1, 0);
			tx2 = std::min(tx2, width);
		}
		if (!repeatY) {
			ty1 = std::max(ty1, 0);
			ty2 = std::min(ty2, height);
		}
		if (tx1 >= tx2 || ty1 >= ty2) {
			return true;
		}

		std::int32_t cx1 = FloorDiv(tx1, ChunkTiles), cx2 = FloorDiv(tx2 - 1, ChunkTiles);
		std::int32_t cy1 = FloorDiv(ty1, ChunkTiles), cy2 = FloorDiv(ty2 - 1, ChunkTiles);
		std::int32_t drawn = 0;
		for (std::int32_t cy = cy1; cy <= cy2; cy++) {
			for (std::int32_t cx = cx1; cx <= cx2; cx++) {
				if (++drawn > MaxImagesPerLayer) {
					return true;
				}
				// Where the pattern of a layer that repeats is at the start of the chunk, the image is the same there
				Vector2i start(repeatX ? Mod(cx * ChunkTiles, width) : cx * ChunkTiles, repeatY ? Mod(cy * ChunkTiles, height) : cy * ChunkTiles);
				Chunk* chunk = FindChunk(layerIndex, start);
				if (chunk == nullptr) {
					if (_buildsLeft <= 0) {
						continue;
					}
					chunk = &_chunks.emplace_back();
					chunk->Layer = layerIndex;
					chunk->Start = start;
					chunk->Dirty = true;
				}
				if (chunk->Dirty && _buildsLeft > 0) {
					_buildsLeft--;
					BuildChunk(*chunk);
				}
				chunk->LastUsed = _frame;
				if (chunk->Image != nullptr) {
					images.push_back(TileMapLayerImage{ chunk->Image.get(), Rectf(origin.X + cx * ChunkTiles * TileSize, origin.Y + cy * ChunkTiles * TileSize,
						ChunkTiles * TileSize, ChunkTiles * TileSize) });
				}
			}
		}

		DropOldChunks();
		return true;
	}

	TileLod::Chunk* TileLod::FindChunk(std::int32_t layer, Vector2i start)
	{
		for (Chunk& chunk : _chunks) {
			if (chunk.Layer == layer && chunk.Start == start) {
				return &chunk;
			}
		}
		return nullptr;
	}

	void TileLod::BuildChunk(Chunk& chunk)
	{
		chunk.Dirty = false;
		const LevelDocument& document = _session.GetDocument();
		if (chunk.Layer >= std::int32_t(document.Layers.size())) {
			return;
		}
		const DocumentLayer& layer = document.Layers[chunk.Layer];
		std::int32_t width = layer.GetWidth(), height = layer.GetHeight();
		bool repeatX = !layer.IsSpriteLayer() && (layer.Record.Flags & Formats::LayerRecord::RepeatX) != 0;
		bool repeatY = !layer.IsSpriteLayer() && (layer.Record.Flags & Formats::LayerRecord::RepeatY) != 0;

		constexpr std::int32_t Size = ChunkTiles * TilePixels;
		_pixels.clear();
		_pixels.resize(std::size_t(Size * Size), 0);
		bool empty = true;
		for (std::int32_t y = 0; y < ChunkTiles; y++) {
			std::int32_t ty = chunk.Start.Y + y;
			if (repeatY) {
				ty = Mod(ty, height);
			} else if (ty >= height) {
				break;
			}
			for (std::int32_t x = 0; x < ChunkTiles; x++) {
				std::int32_t tx = chunk.Start.X + x;
				if (repeatX) {
					tx = Mod(tx, width);
				} else if (tx >= width) {
					break;
				}
				const TileRef& tile = layer.GetTile(tx, ty);
				if (tile.IsEmpty() || (tile.Flags & Formats::TileRecord::ModifierMask) == Formats::TileRecord::ModifierInvisible) {
					continue;
				}
				const TileBlock& block = GetBlock(tile);
				bool translucent = ((tile.Flags & Formats::TileRecord::ModifierMask) == Formats::TileRecord::ModifierTranslucent);
				for (std::int32_t j = 0; j < TilePixels; j++) {
					std::uint32_t* row = &_pixels[std::size_t((y * TilePixels + j) * Size + x * TilePixels)];
					for (std::int32_t i = 0; i < TilePixels; i++) {
						std::uint32_t color = block.Colors[j * TilePixels + i];
						if (translucent) {
							color = (color & 0x00FFFFFFu) | (((color >> 24) * 180 / 255) << 24);
						}
						row[i] = color;
						empty &= ((color >> 24) == 0);
					}
				}
			}
		}

		if (empty) {
			chunk.Image = nullptr;
			return;
		}
		if (chunk.Image == nullptr) {
			chunk.Image = std::make_unique<Texture>("EditorLodChunk", Texture::Format::RGBA8, Size, Size);
			chunk.Image->SetMinFiltering(SamplerFilter::Linear);
			chunk.Image->SetMagFiltering(SamplerFilter::Linear);
			chunk.Image->SetWrap(SamplerWrapping::ClampToEdge);
		}
		chunk.Image->LoadFromTexels(reinterpret_cast<const std::uint8_t*>(_pixels.data()), 0, 0, Size, Size);
	}

	const TileLod::TileBlock& TileLod::GetBlock(const TileRef& tile)
	{
		// Flips change the block, the rest of the flags don't
		std::uint32_t key = (std::uint32_t(tile.Source) << 24) | (std::uint32_t(tile.Flags & 0x03) << 16) | tile.Index;
		auto it = _blocks.find(key);
		if (it != _blocks.end()) {
			return it->second;
		}

		TileBlock& block = _blocks[key];
		std::memset(block.Colors, 0, sizeof(block.Colors));
		LevelThumbnail::TilePixels source;
		TileRef plain = tile;
		plain.Flags &= 0x03;
		if (!LevelThumbnail::GetTilePixels(_session, plain, source)) {
			return block;
		}

		// Each part the average of its pixels, weighted by how opaque they are, and as opaque as they are on average
		const std::uint32_t* palette = ContentResolver::Get().GetPalettes().data();
		constexpr std::int32_t Part = 32 / TilePixels;
		for (std::int32_t j = 0; j < TilePixels; j++) {
			for (std::int32_t i = 0; i < TilePixels; i++) {
				std::uint32_t r = 0, g = 0, b = 0, a = 0;
				for (std::int32_t y = 0; y < Part; y++) {
					for (std::int32_t x = 0; x < Part; x++) {
						std::uint32_t color = source.GetPixel(i * Part + x, j * Part + y, palette);
						std::uint32_t alpha = color >> 24;
						r += (color & 0xFF) * alpha;
						g += ((color >> 8) & 0xFF) * alpha;
						b += ((color >> 16) & 0xFF) * alpha;
						a += alpha;
					}
				}
				if (a > 0) {
					block.Colors[j * TilePixels + i] = (r / a) | ((g / a) << 8) | ((b / a) << 16) | ((a / (Part * Part)) << 24);
				}
			}
		}
		return block;
	}

	void TileLod::DropOldChunks()
	{
		if (_chunks.size() <= MaxChunks) {
			return;
		}
		// Those drawn in this frame stay, even if there are more of them
		std::sort(_chunks.begin(), _chunks.end(), [](const Chunk& a, const Chunk& b) { return a.LastUsed > b.LastUsed; });
		std::size_t keep = MaxChunks;
		while (keep < _chunks.size() && _chunks[keep].LastUsed == _frame) {
			keep++;
		}
		_chunks.erase(_chunks.begin() + keep, _chunks.end());
	}
}

#endif
