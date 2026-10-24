#include "LevelPreview.h"
#include "../EventType.h"

#include <algorithm>
#include <cmath>

#include <IO/FileSystem.h>

using namespace Death::IO;
using namespace nCine;

namespace Jazz2::Formats
{
	namespace
	{
		std::uint32_t Blend(std::uint32_t dst, std::uint32_t src)
		{
			std::uint32_t a = src >> 24;
			if (a == 0) {
				return dst;
			}
			if (a == 255) {
				return src;
			}
			std::uint32_t r = ((src & 0xFF) * a + (dst & 0xFF) * (255 - a)) / 255;
			std::uint32_t g = (((src >> 8) & 0xFF) * a + ((dst >> 8) & 0xFF) * (255 - a)) / 255;
			std::uint32_t b = (((src >> 16) & 0xFF) * a + ((dst >> 16) & 0xFF) * (255 - a)) / 255;
			return r | (g << 8) | (b << 16) | 0xFF000000u;
		}

		// The depth the game orders the layers by, the sprite layer is always at 50 (see Tiles::LayerDescription::FromRecord())
		std::int32_t GetDrawDepth(const LayerRecord& layer)
		{
			return (layer.Kind == LayerKind::Sprite ? 50 : layer.Depth);
		}

		// Each pixel the average of a few points of the level
		constexpr std::int32_t Samples = LevelPreview::Scale;
		constexpr std::int32_t SampleW = LevelPreview::Width * Samples, SampleH = LevelPreview::Height * Samples;
		constexpr float SampleStep = float(LevelPreview::Scale) / Samples;

		// The view of the game around the start, kept within the level as the camera is, and the size of the sprite layer
		// in tiles, returns `false` for a level without a sprite layer
		bool GetView(LevelPreview::ISource& source, Vector2f start, Rectf& view, Vector2i& levelSize)
		{
			// The last sprite layer is the one, as in the game
			std::int32_t spriteLayer = source.GetLayerCount() - 1;
			while (spriteLayer >= 0 && source.GetLayer(spriteLayer).Kind != LayerKind::Sprite) {
				spriteLayer--;
			}
			if (spriteLayer < 0) {
				return false;
			}
			std::int32_t width = source.GetLayer(spriteLayer).Width, height = source.GetLayer(spriteLayer).Height;
			if (width <= 0 || height <= 0) {
				return false;
			}

			constexpr float ViewW = float(LevelPreview::Width * LevelPreview::Scale), ViewH = float(LevelPreview::Height * LevelPreview::Scale);
			float levelW = width * 32.0f, levelH = height * 32.0f;
			Vector2f center(levelW > ViewW ? std::clamp(start.X, ViewW * 0.5f, levelW - ViewW * 0.5f) : levelW * 0.5f,
				levelH > ViewH ? std::clamp(start.Y, ViewH * 0.5f, levelH - ViewH * 0.5f) : levelH * 0.5f);
			view = Rectf(std::round(center.X - ViewW * 0.5f), std::round(center.Y - ViewH * 0.5f), ViewW, ViewH);
			levelSize = Vector2i(width, height);
			return true;
		}

		// The layers the preview draws, in the order the game draws them: by their depth, whatever order the level stores
		// them in, as an extra layer of a converted level comes after the eight of the original format; the ones of the same depth from
		// the last one on. Not the hidden ones and the layers of effects (x-ray, blur behind and chroma key).
		SmallVector<std::int32_t, 16> GetDrawnLayers(LevelPreview::ISource& source)
		{
			std::int32_t layerCount = source.GetLayerCount();
			SmallVector<std::int32_t, 16> order;
			order.reserve(layerCount);
			for (std::int32_t l = layerCount - 1; l >= 0; l--) {
				const LayerRecord& layer = source.GetLayer(l);
				auto rendererType = Tiles::LayerRendererType(layer.RendererType);
				if ((layer.Flags & LayerRecord::Visible) != 0 && rendererType != Tiles::LayerRendererType::XRay &&
					rendererType != Tiles::LayerRendererType::BlurBehind && rendererType != Tiles::LayerRendererType::ChromaKey) {
					order.push_back(l);
				}
			}
			std::stable_sort(order.begin(), order.end(), [&source](std::int32_t a, std::int32_t b) {
				return GetDrawDepth(source.GetLayer(a)) > GetDrawDepth(source.GetLayer(b));
			});
			return order;
		}

		// Calls `sample(sx, sy, tx, ty, px, py)` for every point of the preview a layer of tiles covers, row by row, with the
		// tile of the layer there and the pixel of the layer
		template<class Func>
		void ForEachTileSample(LevelPreview::ISource& source, std::int32_t l, const Rectf& view, Vector2i levelSize, Func&& sample)
		{
			const LayerRecord& layer = source.GetLayer(l);
			std::int32_t layerW = layer.Width, layerH = layer.Height;
			if (layerW <= 0 || layerH <= 0) {
				return;
			}
			bool isSprite = (layer.Kind == LayerKind::Sprite);
			bool repeatX = !isSprite && (layer.Flags & LayerRecord::RepeatX) != 0;
			bool repeatY = !isSprite && (layer.Flags & LayerRecord::RepeatY) != 0;
			Vector2f origin = source.GetLayerDescription(l).GetGameplayOrigin(Vector2i(layerW, layerH), levelSize, view);

			for (std::int32_t sy = 0; sy < SampleH; sy++) {
				std::int32_t py = std::int32_t(std::floor(view.Y + (sy + 0.5f) * SampleStep - origin.Y));
				std::int32_t ty = (py >= 0 ? py / 32 : -1 - (-1 - py) / 32);
				if (repeatY) {
					ty = ((ty % layerH) + layerH) % layerH;
				} else if (ty < 0 || ty >= layerH) {
					continue;
				}
				for (std::int32_t sx = 0; sx < SampleW; sx++) {
					std::int32_t px = std::int32_t(std::floor(view.X + (sx + 0.5f) * SampleStep - origin.X));
					std::int32_t tx = (px >= 0 ? px / 32 : -1 - (-1 - px) / 32);
					if (repeatX) {
						tx = ((tx % layerW) + layerW) % layerW;
					} else if (tx < 0 || tx >= layerW) {
						continue;
					}
					sample(sx, sy, tx, ty, px, py);
				}
			}
		}
	}

	std::uint32_t LevelPreview::TilePixels::GetPixel(std::int32_t x, std::int32_t y, const std::uint32_t* palette) const
	{
		if ((Flags & TileRecord::FlipX) != 0) {
			x = 31 - x;
		}
		if ((Flags & TileRecord::FlipY) != 0) {
			y = 31 - y;
		}
		std::int32_t i = y * 32 + x;
		if (TrueColor) {
			return Pixels[i * 4] | (Pixels[i * 4 + 1] << 8) | (Pixels[i * 4 + 2] << 16) | (std::uint32_t(Pixels[i * 4 + 3]) << 24);
		}
		std::uint8_t entry = Pixels[i];
		if (entry == 0) {
			return 0;
		}
		return palette[Remapping != nullptr ? Remapping[entry] : entry];
	}

	Tiles::LayerDescription LevelPreview::ISource::GetLayerDescription(std::int32_t index)
	{
		// The depth of a layer doesn't matter to the preview
		return Tiles::LayerDescription::FromRecord(GetLayer(index), 0);
	}

	std::int32_t LevelPreview::RecordSource::GetLayerCount()
	{
		return std::int32_t(Layers.size());
	}

	const LayerRecord& LevelPreview::RecordSource::GetLayer(std::int32_t index)
	{
		return Layers[index].Record;
	}

	bool LevelPreview::RecordSource::GetTilePixels(std::int32_t layer, std::int32_t x, std::int32_t y, TilePixels& result)
	{
		TileRecord tile;
		if (!ResolveTile(layer, x, y, tile)) {
			return false;
		}

		result.Remapping = nullptr;
		result.TrueColor = false;
		result.Flags = tile.Flags;
		// Palette indices of the level, whatever tile set the tile is of
		if (const std::uint8_t* indices = FindDiffuseOverride(tile.TileId)) {
			result.Pixels = indices;
			return true;
		}

		std::int32_t tileSetIndex, index;
		if (!FindTileSet(tile.TileId, tileSetIndex, index)) {
			return false;
		}
		const TileSet& tileSet = TileSets[tileSetIndex];
		if (tileSet.Data == nullptr) {
			return false;
		}
		std::int32_t slot = index;
		if (!tileSet.Tiles.empty()) {
			auto it = std::lower_bound(tileSet.Tiles.begin(), tileSet.Tiles.end(), index);
			if (it == tileSet.Tiles.end() || *it != index) {
				return false;
			}
			slot = std::int32_t(it - tileSet.Tiles.begin());
		}
		if (slot >= tileSet.Data->TileCount) {
			return false;
		}
		result.Pixels = &tileSet.Data->Images[std::size_t(slot) * TileSetData::ImageBytesPerTile];
		result.TrueColor = (tileSet.Data->TrueColor[slot] != 0);
		result.Remapping = tileSet.Remapping;
		return true;
	}

	void LevelPreview::RecordSource::CollectTiles(Vector2f start, Function<void(std::int32_t, std::int32_t)>&& visit)
	{
		Rectf view;
		Vector2i levelSize;
		if (!GetView(*this, start, view, levelSize)) {
			return;
		}

		for (std::int32_t l : GetDrawnLayers(*this)) {
			const LayerRecord& layer = GetLayer(l);
			auto rendererType = Tiles::LayerRendererType(layer.RendererType);
			if (rendererType == Tiles::LayerRendererType::Reflection || rendererType == Tiles::LayerRendererType::Solid) {
				continue;
			}

			// The same tiles the samples of Make() read, each run of samples in one tile only once
			std::int32_t lastTileIndex = -1;
			ForEachTileSample(*this, l, view, levelSize, [&](std::int32_t, std::int32_t, std::int32_t tx, std::int32_t ty, std::int32_t, std::int32_t) {
				std::int32_t tileIndex = ty * layer.Width + tx;
				if (tileIndex == lastTileIndex) {
					return;
				}
				lastTileIndex = tileIndex;

				TileRecord tile;
				std::int32_t tileSet, index;
				if (ResolveTile(l, tx, ty, tile) && (tile.Flags & TileRecord::ModifierMask) != TileRecord::ModifierInvisible &&
					FindDiffuseOverride(tile.TileId) == nullptr && FindTileSet(tile.TileId, tileSet, index)) {
					visit(tileSet, index);
				}
			});
		}
	}

	bool LevelPreview::RecordSource::ResolveTile(std::int32_t layer, std::int32_t x, std::int32_t y, TileRecord& tile) const
	{
		const Layer& source = Layers[layer];
		tile = source.Tiles[std::size_t(y) * std::size_t(source.Record.Width) + std::size_t(x)];
		if (tile.TileId >= AnimatedTilesOffset) {
			// The first frame stands for the animation
			std::int32_t animation = tile.TileId - AnimatedTilesOffset;
			if (animation >= std::int32_t(AnimatedTiles.size()) || AnimatedTiles[animation].Frames.empty()) {
				return false;
			}
			std::uint8_t flags = tile.Flags;
			tile = AnimatedTiles[animation].Frames[0];
			tile.Flags ^= (flags & (TileRecord::FlipX | TileRecord::FlipY));
			// A modifier of the tile in the layer, translucent or invisible, is what the layer shows the animation with
			if ((flags & TileRecord::ModifierMask) != 0) {
				tile.Flags = std::uint8_t((tile.Flags & ~TileRecord::ModifierMask) | (flags & TileRecord::ModifierMask));
			}
			if (tile.TileId >= AnimatedTilesOffset) {
				return false;
			}
		}
		return (tile.TileId != 0);
	}

	const std::uint8_t* LevelPreview::RecordSource::FindDiffuseOverride(std::uint16_t tileId)
	{
		// The editor keeps the list sorted, so it's searched then, a list from anywhere else is only checked for it once
		if (_overridesSorted < 0) {
			_overridesSorted = (std::is_sorted(DiffuseOverrides.begin(), DiffuseOverrides.end(), [](const TileDiffuseOverride& a, const TileDiffuseOverride& b) {
				return (a.TileId < b.TileId);
			}) ? 1 : 0);
		}
		if (_overridesSorted != 0) {
			auto it = std::lower_bound(DiffuseOverrides.begin(), DiffuseOverrides.end(), tileId, [](const TileDiffuseOverride& diffuse, std::uint16_t tileId) {
				return (diffuse.TileId < tileId);
			});
			if (it != DiffuseOverrides.end() && it->TileId == tileId) {
				return it->Indices;
			}
		} else {
			for (const TileDiffuseOverride& diffuse : DiffuseOverrides) {
				if (diffuse.TileId == tileId) {
					return diffuse.Indices;
				}
			}
		}
		return nullptr;
	}

	bool LevelPreview::RecordSource::FindTileSet(std::int32_t tileId, std::int32_t& tileSet, std::int32_t& index) const
	{
		// The tile IDs count the tiles of the tile sets in their order
		std::int32_t remaining = tileId;
		for (std::size_t i = 0; i < TileSets.size(); i++) {
			if (remaining < TileSets[i].Count) {
				tileSet = std::int32_t(i);
				index = remaining + TileSets[i].Offset;
				return true;
			}
			remaining -= TileSets[i].Count;
		}
		return false;
	}

	void LevelPreview::StartPicker::Add(const EventRecord& event, std::int32_t x, std::int32_t y)
	{
		std::int32_t priority;
		if (EventType(event.Type) == EventType::LevelStart) {
			priority = ((event.Params[0] & 0x01) != 0 ? 0 : 1);
		} else if (EventType(event.Type) == EventType::LevelStartMultiplayer) {
			priority = 2;
		} else {
			return;
		}
		if (priority < _priority) {
			_priority = priority;
			_position = Vector2f(float(x) * 32.0f + 16.0f, float(y) * 32.0f + 8.0f);
		}
	}

	Vector2f LevelPreview::StartPicker::GetPosition(std::int32_t width, std::int32_t height) const
	{
		return (_priority != INT32_MAX ? _position : Vector2f(width * 16.0f, height * 16.0f));
	}

	void LevelPreview::Make(ISource& source, Vector2f start, const std::uint32_t* palette, SmallVectorImpl<std::uint8_t>& data)
	{
		data.clear();
		Rectf view;
		Vector2i levelSize;
		if (!GetView(source, start, view, levelSize)) {
			return;
		}
		std::int32_t width = levelSize.X, height = levelSize.Y;

		// Each pixel the average of a few points of the level, all layers from the farthest one on, at the parallax
		// of the game; just the color of a layer of solid color, what is behind a reflection mirrored, and nothing of
		// the other layers of effects
		SmallVector<std::uint32_t, 0> accum;
		accum.resize(std::size_t(Width * Height * Samples * Samples), 0xFF201810u);

		for (std::int32_t l : GetDrawnLayers(source)) {
			const LayerRecord& layer = source.GetLayer(l);
			auto rendererType = Tiles::LayerRendererType(layer.RendererType);
			if (rendererType == Tiles::LayerRendererType::Reflection) {
				// What is behind it mirrored at its horizon and tinted, as the game draws it without the ripples its tiles make
				// (see Tiles::LayerRendererType::Reflection); with nothing above the horizon, it's black. Drawn only where the
				// layer is placed, if it's drawn so, the horizon a fraction of the layer then too
				std::int32_t x0 = 0, x1 = SampleW, y0 = 0, y1 = SampleH;
				float referenceY = 0.0f, referenceH = float(SampleH);
				if ((layer.Textured.Flags & (TexturedLayerParams::InnerWindow | TexturedLayerParams::InnerLayer)) != 0 && layer.Width > 0 && layer.Height > 0) {
					Vector2f origin = source.GetLayerDescription(l).GetGameplayOrigin(Vector2i(layer.Width, layer.Height), Vector2i(width, height), view);
					if ((layer.Flags & LayerRecord::RepeatX) == 0) {
						x0 = std::clamp<std::int32_t>(std::int32_t(std::floor((origin.X - view.X) / SampleStep)), 0, SampleW);
						x1 = std::clamp<std::int32_t>(std::int32_t(std::ceil((origin.X + layer.Width * 32.0f - view.X) / SampleStep)), 0, SampleW);
					}
					if ((layer.Flags & LayerRecord::RepeatY) == 0) {
						float top = (origin.Y - view.Y) / SampleStep;
						float bottom = (origin.Y + layer.Height * 32.0f - view.Y) / SampleStep;
						y0 = std::clamp<std::int32_t>(std::int32_t(std::floor(top)), 0, SampleH);
						y1 = std::clamp<std::int32_t>(std::int32_t(std::ceil(bottom)), 0, SampleH);
						if ((layer.Textured.Flags & TexturedLayerParams::InnerLayer) != 0) {
							referenceY = top;
							referenceH = bottom - top;
						}
					}
				}
				std::int32_t horizon = std::int32_t(std::floor(referenceY + std::clamp<float>(layer.Textured.FadeY, 0.0f, 1.0f) * referenceH + 0.5f));
				std::uint32_t tint = (palette[layer.Textured.Fade] & 0x00FFFFFFu) | (std::uint32_t(layer.Color[0]) << 24);
				for (std::int32_t sy = std::max<std::int32_t>(horizon, y0); sy < y1; sy++) {
					// Above the horizon only, so never a row already mirrored; the top row repeats where there are fewer rows above
					std::int32_t from = std::max<std::int32_t>(2 * horizon - 1 - sy, 0);
					for (std::int32_t sx = x0; sx < x1; sx++) {
						std::uint32_t color = (horizon > 0 ? accum[std::size_t(from) * SampleW + std::size_t(sx)] : 0xFF000000u);
						accum[std::size_t(sy) * SampleW + std::size_t(sx)] = Blend(color, tint);
					}
				}
				continue;
			}
			if (rendererType == Tiles::LayerRendererType::Solid) {
				std::uint32_t color = layer.Color[0] | (layer.Color[1] << 8) | (layer.Color[2] << 16) | (std::uint32_t(layer.Color[3]) << 24);
				for (std::uint32_t& pixel : accum) {
					pixel = Blend(pixel, color);
				}
				continue;
			}

			// A palette-shifted layer takes the colors further in the palette, as the game draws it (see
			// ContentResolver::AcquireShiftedPaletteOffset())
			std::uint32_t shiftedPalette[256];
			const std::uint32_t* layerPalette = palette;
			if (rendererType == Tiles::LayerRendererType::PaletteShift && layer.Color[0] != 0) {
				shiftedPalette[0] = palette[0];
				for (std::int32_t i = 1; i < 256; i++) {
					std::int32_t from = (i + layer.Color[0]) & 0xFF;
					shiftedPalette[i] = (from == 0 ? (palette[0] | 0xFF000000u) : palette[from]);
				}
				layerPalette = shiftedPalette;
			}
			// A single color layer is the shapes of its tiles in that entry of the palette, as opaque as the layer is
			const bool singleColor = (rendererType == Tiles::LayerRendererType::SingleColor);
			const std::uint32_t singleColorRgb = (palette[layer.Color[0]] & 0x00FFFFFFu);

			std::int32_t lastTileIndex = -1;
			TilePixels tilePixels;
			bool tileValid = false;
			ForEachTileSample(source, l, view, levelSize, [&](std::int32_t sx, std::int32_t sy, std::int32_t tx, std::int32_t ty, std::int32_t px, std::int32_t py) {
				std::int32_t tileIndex = ty * layer.Width + tx;
				if (tileIndex != lastTileIndex) {
					lastTileIndex = tileIndex;
					// The modifier as resolved, an animation's own frame can be the invisible one too
					tileValid = (source.GetTilePixels(l, tx, ty, tilePixels) &&
						(tilePixels.Flags & TileRecord::ModifierMask) != TileRecord::ModifierInvisible);
				}
				if (!tileValid) {
					return;
				}
				std::uint32_t color = tilePixels.GetPixel(((px % 32) + 32) % 32, ((py % 32) + 32) % 32, layerPalette);
				if (singleColor) {
					color = singleColorRgb | (((color >> 24) * layer.Color[3] / 255) << 24);
				}
				if ((tilePixels.Flags & TileRecord::ModifierMask) == TileRecord::ModifierTranslucent) {
					// As translucent as the game draws them, see Tiles::TileMap::TranslucentTileAlpha
					color = (color & 0x00FFFFFFu) | (((color >> 24) * 192 / 255) << 24);
				}
				std::uint32_t& pixel = accum[std::size_t(sy) * SampleW + std::size_t(sx)];
				pixel = Blend(pixel, color);
			});
		}

		data.push_back(std::uint8_t(Width));
		data.push_back(std::uint8_t(Width >> 8));
		data.push_back(std::uint8_t(Height));
		data.push_back(std::uint8_t(Height >> 8));
		for (std::int32_t y = 0; y < Height; y++) {
			for (std::int32_t x = 0; x < Width; x++) {
				std::uint32_t r = 0, g = 0, b = 0;
				for (std::int32_t j = 0; j < Samples; j++) {
					for (std::int32_t i = 0; i < Samples; i++) {
						std::uint32_t color = accum[std::size_t(y * Samples + j) * SampleW + std::size_t(x * Samples + i)];
						r += color & 0xFF;
						g += (color >> 8) & 0xFF;
						b += (color >> 16) & 0xFF;
					}
				}
				data.push_back(std::uint8_t(r / (Samples * Samples)));
				data.push_back(std::uint8_t(g / (Samples * Samples)));
				data.push_back(std::uint8_t(b / (Samples * Samples)));
			}
		}
	}

	bool LevelPreview::Read(StringView path, SmallVectorImpl<std::uint8_t>& rgba, Vector2i& size)
	{
		// The editor and the converter store previews of at most 128x72 pixels, anything much larger isn't one of them
		constexpr std::int32_t MaxSize = 256;

		rgba.clear();
		size = Vector2i();

		auto s = fs::Open(path, FileAccess::Read);
		LevelHeader header;
		if (!s->IsValid() || !LevelFile::ReadHeader(*s, header)) {
			return false;
		}
		LevelReader reader(*s, header);
		LevelProperties properties;
		reader.ReadProperties(properties);
		if (!reader.SkipToChunks()) {
			return false;
		}
		SmallVector<ChunkRecord, 0> chunks;
		reader.ReadSelectedChunks(chunks, [](std::uint32_t tag, std::uint32_t) {
			return (tag == LevelFile::PreviewChunk);
		});
		for (const ChunkRecord& chunk : chunks) {
			if (chunk.Tag != LevelFile::PreviewChunk || chunk.Data.size() < 4) {
				continue;
			}
			std::int32_t width = chunk.Data[0] | (chunk.Data[1] << 8);
			std::int32_t height = chunk.Data[2] | (chunk.Data[3] << 8);
			if (width <= 0 || height <= 0 || width > MaxSize || height > MaxSize ||
				chunk.Data.size() < 4 + std::size_t(width) * std::size_t(height) * 3) {
				return false;
			}
			const std::size_t pixelCount = std::size_t(width) * std::size_t(height);
			rgba.resize_for_overwrite(pixelCount * 4);
			for (std::size_t i = 0; i < pixelCount; i++) {
				rgba[i * 4] = chunk.Data[4 + i * 3];
				rgba[i * 4 + 1] = chunk.Data[4 + i * 3 + 1];
				rgba[i * 4 + 2] = chunk.Data[4 + i * 3 + 2];
				rgba[i * 4 + 3] = 255;
			}
			size = Vector2i(width, height);
			return true;
		}
		return false;
	}
}
