#include "LevelPreview.h"
#include "../EventType.h"

#include <algorithm>
#include <cmath>

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
		const Layer& source = Layers[layer];
		TileRecord tile = source.Tiles[std::size_t(y) * std::size_t(source.Record.Width) + std::size_t(x)];
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
		if (tile.TileId == 0) {
			return false;
		}

		result.Remapping = nullptr;
		result.TrueColor = false;
		result.Flags = tile.Flags;
		// Palette indices of the level, whatever tile set the tile is of; the editor keeps the list sorted, so it's searched
		// then, a list from anywhere else is only checked for it once
		if (_overridesSorted < 0) {
			_overridesSorted = (std::is_sorted(DiffuseOverrides.begin(), DiffuseOverrides.end(), [](const TileDiffuseOverride& a, const TileDiffuseOverride& b) {
				return (a.TileId < b.TileId);
			}) ? 1 : 0);
		}
		if (_overridesSorted != 0) {
			auto it = std::lower_bound(DiffuseOverrides.begin(), DiffuseOverrides.end(), tile.TileId, [](const TileDiffuseOverride& diffuse, std::uint16_t tileId) {
				return (diffuse.TileId < tileId);
			});
			if (it != DiffuseOverrides.end() && it->TileId == tile.TileId) {
				result.Pixels = it->Indices;
				return true;
			}
		} else {
			for (const TileDiffuseOverride& diffuse : DiffuseOverrides) {
				if (diffuse.TileId == tile.TileId) {
					result.Pixels = diffuse.Indices;
					return true;
				}
			}
		}

		std::int32_t index = tile.TileId;
		for (const TileSet& tileSet : TileSets) {
			if (index < tileSet.Count) {
				index += tileSet.Offset;
				if (tileSet.Data == nullptr || index >= tileSet.Data->TileCount) {
					return false;
				}
				result.Pixels = &tileSet.Data->Images[std::size_t(index) * TileSetData::ImageBytesPerTile];
				result.TrueColor = (tileSet.Data->TrueColor[index] != 0);
				result.Remapping = tileSet.Remapping;
				return true;
			}
			index -= tileSet.Count;
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
		// The last sprite layer is the one, as in the game
		std::int32_t layerCount = source.GetLayerCount();
		std::int32_t spriteLayer = layerCount - 1;
		while (spriteLayer >= 0 && source.GetLayer(spriteLayer).Kind != LayerKind::Sprite) {
			spriteLayer--;
		}
		if (spriteLayer < 0) {
			return;
		}
		std::int32_t width = source.GetLayer(spriteLayer).Width, height = source.GetLayer(spriteLayer).Height;
		if (width <= 0 || height <= 0) {
			return;
		}

		// The view of the game there, kept within the level as the camera is
		constexpr float ViewW = float(Width * Scale), ViewH = float(Height * Scale);
		float levelW = width * 32.0f, levelH = height * 32.0f;
		Vector2f center(levelW > ViewW ? std::clamp(start.X, ViewW * 0.5f, levelW - ViewW * 0.5f) : levelW * 0.5f,
			levelH > ViewH ? std::clamp(start.Y, ViewH * 0.5f, levelH - ViewH * 0.5f) : levelH * 0.5f);
		Rectf view(std::round(center.X - ViewW * 0.5f), std::round(center.Y - ViewH * 0.5f), ViewW, ViewH);

		// Each pixel the average of a few points of the level, all layers from the farthest one on, at the parallax
		// of the game; just the color of a layer of solid color, what is behind a reflection mirrored, and nothing of
		// the other layers of effects
		constexpr std::int32_t Samples = Scale;
		SmallVector<std::uint32_t, 0> accum;
		accum.resize(std::size_t(Width * Height * Samples * Samples), 0xFF201810u);
		constexpr std::int32_t SampleW = Width * Samples, SampleH = Height * Samples;
		constexpr float SampleStep = float(Scale) / Samples;

		// The game draws the layers by their depth, whatever order the level stores them in, as an extra layer of MLLE
		// comes after the eight of the original format; the ones of the same depth from the last one on
		SmallVector<std::int32_t, 16> order;
		order.reserve(layerCount);
		for (std::int32_t l = layerCount - 1; l >= 0; l--) {
			order.push_back(l);
		}
		std::stable_sort(order.begin(), order.end(), [&source](std::int32_t a, std::int32_t b) {
			return GetDrawDepth(source.GetLayer(a)) > GetDrawDepth(source.GetLayer(b));
		});

		for (std::int32_t l : order) {
			const LayerRecord& layer = source.GetLayer(l);
			if ((layer.Flags & LayerRecord::Visible) == 0) {
				continue;
			}
			auto rendererType = Tiles::LayerRendererType(layer.RendererType);
			if (rendererType == Tiles::LayerRendererType::XRay || rendererType == Tiles::LayerRendererType::BlurBehind) {
				continue;
			}
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

			std::int32_t layerW = layer.Width, layerH = layer.Height;
			if (layerW <= 0 || layerH <= 0) {
				continue;
			}
			bool isSprite = (layer.Kind == LayerKind::Sprite);
			bool repeatX = !isSprite && (layer.Flags & LayerRecord::RepeatX) != 0;
			bool repeatY = !isSprite && (layer.Flags & LayerRecord::RepeatY) != 0;
			Vector2f origin = source.GetLayerDescription(l).GetGameplayOrigin(Vector2i(layerW, layerH), Vector2i(width, height), view);

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

			std::int32_t lastTileIndex = -1;
			TilePixels tilePixels;
			bool tileValid = false;
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
					std::int32_t tileIndex = ty * layerW + tx;
					if (tileIndex != lastTileIndex) {
						lastTileIndex = tileIndex;
						// The modifier as resolved, an animation's own frame can be the invisible one too
						tileValid = (source.GetTilePixels(l, tx, ty, tilePixels) &&
							(tilePixels.Flags & TileRecord::ModifierMask) != TileRecord::ModifierInvisible);
					}
					if (!tileValid) {
						continue;
					}
					std::uint32_t color = tilePixels.GetPixel(((px % 32) + 32) % 32, ((py % 32) + 32) % 32, layerPalette);
					if ((tilePixels.Flags & TileRecord::ModifierMask) == TileRecord::ModifierTranslucent) {
						// As translucent as the game draws them, see Tiles::TileMap::TranslucentTileAlpha
						color = (color & 0x00FFFFFFu) | (((color >> 24) * 192 / 255) << 24);
					}
					std::uint32_t& pixel = accum[std::size_t(sy) * SampleW + std::size_t(sx)];
					pixel = Blend(pixel, color);
				}
			}
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
}
