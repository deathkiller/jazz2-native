#include "LevelThumbnail.h"

#if defined(WITH_EDITOR)

#include "../EditorSession.h"
#include "../../ContentResolver.h"
#include "../../EventType.h"
#include "../../Formats/TileSetFile.h"
#include "../../Tiles/TileMap.h"

#include <algorithm>

namespace Jazz2::Editor
{
	LevelThumbnail::LevelThumbnail(EditorSession& session)
		: _session(session)
	{
	}

	void LevelThumbnail::Clear()
	{
		_colors.clear();
	}

	std::uint32_t LevelThumbnail::GetTileColor(TileRef tile)
	{
		std::uint32_t key = (std::uint32_t(tile.Source) << 16) | tile.Index;
		auto it = _colors.find(key);
		if (it == _colors.end()) {
			it = _colors.emplace(key, ComputeTileColor(tile)).first;
		}
		return it->second;
	}

	bool LevelThumbnail::GetTilePixels(EditorSession& session, TileRef tile, TilePixels& result)
	{
		const LevelDocument& document = session.GetDocument();
		if (tile.IsAnimated()) {
			// The first frame stands for the animation
			if (tile.Index >= document.Animations.size() || document.Animations[tile.Index].Frames.empty()) {
				return false;
			}
			std::uint8_t flags = tile.Flags;
			tile = document.Animations[tile.Index].Frames[0];
			tile.Flags ^= (flags & (Formats::TileRecord::FlipX | Formats::TileRecord::FlipY));
			if (tile.IsAnimated()) {
				return false;
			}
		}
		if (tile.IsEmpty() || tile.Source == TileRef::Raw) {
			return false;
		}

		StringView name;
		std::int32_t offset = 0;
		const Formats::ExtraTileSetRecord* extra = nullptr;
		if (tile.Source == TileRef::DefaultTileSet) {
			name = document.Properties.DefaultTileSet;
		} else if (std::size_t(tile.Source) - 1 < document.ExtraTileSets.size()) {
			extra = &document.ExtraTileSets[tile.Source - 1];
			name = extra->Name;
			offset = extra->Offset;
		} else {
			return false;
		}
		const Formats::TileSetData* tileSet = session.GetTileSetData(name);
		std::int32_t index = offset + tile.Index;
		if (tileSet == nullptr || index >= tileSet->TileCount) {
			return false;
		}

		result.Pixels = &tileSet->Images[std::size_t(index) * Formats::TileSetData::ImageBytesPerTile];
		result.TrueColor = (tileSet->TrueColor[index] != 0);
		result.Remapping = (extra != nullptr && (extra->Flags & Formats::ExtraTileSetRecord::Remapped) != 0 &&
			(extra->Flags & Formats::ExtraTileSetRecord::TrueColor) == 0 ? extra->Remapping : nullptr);
		result.Flags = tile.Flags;
		return true;
	}

	std::uint32_t LevelThumbnail::TilePixels::GetPixel(std::int32_t x, std::int32_t y, const std::uint32_t* palette) const
	{
		if ((Flags & Formats::TileRecord::FlipX) != 0) {
			x = 31 - x;
		}
		if ((Flags & Formats::TileRecord::FlipY) != 0) {
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

	std::uint32_t LevelThumbnail::ComputeTileColor(TileRef tile)
	{
		TilePixels source;
		if (!GetTilePixels(_session, tile, source)) {
			return 0;
		}

		const std::uint32_t* palette = ContentResolver::Get().GetPalettes().data();
		std::uint32_t r = 0, g = 0, b = 0, count = 0;
		for (std::int32_t i = 0; i < 32 * 32; i++) {
			std::uint32_t color = source.GetPixel(i % 32, i / 32, palette);
			if ((color >> 24) < 128) {
				continue;
			}
			r += color & 0xFF;
			g += (color >> 8) & 0xFF;
			b += (color >> 16) & 0xFF;
			count++;
		}
		if (count == 0) {
			return 0;
		}
		// Half covered counts as covered, a tile with a little of something on it as a little
		std::uint32_t alpha = std::min<std::uint32_t>(255, count * 255 / 512);
		return (r / count) | ((g / count) << 8) | ((b / count) << 16) | (alpha << 24);
	}

	void LevelThumbnail::DrawLayer(std::int32_t layerIndex, SmallVectorImpl<std::uint32_t>& pixels)
	{
		const LevelDocument& document = _session.GetDocument();
		pixels.clear();
		if (layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size())) {
			return;
		}
		const DocumentLayer& layer = document.Layers[layerIndex];
		pixels.reserve(layer.Tiles.size());
		for (const TileRef& tile : layer.Tiles) {
			pixels.push_back(GetTileColor(tile));
		}
	}

	void LevelThumbnail::MakePreview(EditorSession& session, SmallVectorImpl<std::uint8_t>& data)
	{
		data.clear();
		const LevelDocument& document = session.GetDocument();
		std::int32_t spriteLayer = document.GetSpriteLayerIndex();
		if (spriteLayer < 0) {
			return;
		}
		const DocumentLayer& spriteLayerData = document.Layers[spriteLayer];
		std::int32_t width = spriteLayerData.GetWidth(), height = spriteLayerData.GetHeight();
		if (width <= 0 || height <= 0) {
			return;
		}

		// Where the level starts, Jazz first, then anyone, then a start of multiplayer, else the middle of the level
		Vector2f start(width * 16.0f, height * 16.0f);
		{
			std::int32_t gridWidth = std::max(document.GetEventGridSize().X, 1);
			std::int32_t best = INT32_MAX;
			for (const DocumentEvent& event : document.Events) {
				std::int32_t rank;
				if (EventType(event.Record.Type) == EventType::LevelStart) {
					rank = ((event.Record.Params[0] & 0x01) != 0 ? 0 : 1);
				} else if (EventType(event.Record.Type) == EventType::LevelStartMultiplayer) {
					rank = 2;
				} else {
					continue;
				}
				if (rank < best) {
					best = rank;
					start = Vector2f(float(event.Cell % std::uint32_t(gridWidth)) * 32.0f + 16.0f,
						float(event.Cell / std::uint32_t(gridWidth)) * 32.0f + 8.0f);
				}
			}
		}

		// The view of the game there, kept within the level as the camera is
		constexpr float ViewW = float(PreviewMaxWidth * PreviewScale), ViewH = float(PreviewMaxHeight * PreviewScale);
		float levelW = width * 32.0f, levelH = height * 32.0f;
		Vector2f center(levelW > ViewW ? std::clamp(start.X, ViewW * 0.5f, levelW - ViewW * 0.5f) : levelW * 0.5f,
			levelH > ViewH ? std::clamp(start.Y, ViewH * 0.5f, levelH - ViewH * 0.5f) : levelH * 0.5f);
		Rectf view(std::round(center.X - ViewW * 0.5f), std::round(center.Y - ViewH * 0.5f), ViewW, ViewH);

		// Each pixel the average of a few points of the level, all layers from the farthest one on, at the parallax
		// of the game; just the color of a layer of solid color, and nothing of layers of effects
		constexpr std::int32_t Samples = PreviewScale;
		SmallVector<std::uint32_t, 0> accum;
		accum.resize(std::size_t(PreviewMaxWidth * PreviewMaxHeight * Samples * Samples), 0xFF201810u);
		const std::uint32_t* palette = ContentResolver::Get().GetPalettes().data();
		Tiles::TileMap* tileMap = session.GetTileMap();
		constexpr std::int32_t SampleW = PreviewMaxWidth * Samples, SampleH = PreviewMaxHeight * Samples;
		constexpr float SampleStep = float(PreviewScale) / Samples;

		for (std::int32_t l = std::int32_t(document.Layers.size()) - 1; l >= 0; l--) {
			const DocumentLayer& layer = document.Layers[l];
			if ((layer.Record.Flags & Formats::LayerRecord::Visible) == 0) {
				continue;
			}
			auto rendererType = Tiles::LayerRendererType(layer.Record.RendererType);
			if (rendererType == Tiles::LayerRendererType::XRay || rendererType == Tiles::LayerRendererType::BlurBehind) {
				continue;
			}
			if (rendererType == Tiles::LayerRendererType::Solid) {
				std::uint32_t color = layer.Record.Color[0] | (layer.Record.Color[1] << 8) | (layer.Record.Color[2] << 16) | (std::uint32_t(layer.Record.Color[3]) << 24);
				for (std::uint32_t& pixel : accum) {
					pixel = Blend(pixel, color);
				}
				continue;
			}

			std::int32_t layerW = layer.GetWidth(), layerH = layer.GetHeight();
			if (layerW <= 0 || layerH <= 0) {
				continue;
			}
			bool repeatX = !layer.IsSpriteLayer() && (layer.Record.Flags & Formats::LayerRecord::RepeatX) != 0;
			bool repeatY = !layer.IsSpriteLayer() && (layer.Record.Flags & Formats::LayerRecord::RepeatY) != 0;
			Vector2f origin = (tileMap != nullptr ? tileMap->GetGameplayLayerOrigin(l, view) : Vector2f::Zero);

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
						const TileRef& tile = layer.Tiles[std::size_t(tileIndex)];
						tileValid = ((tile.Flags & Formats::TileRecord::ModifierMask) != Formats::TileRecord::ModifierInvisible &&
							GetTilePixels(session, tile, tilePixels));
					}
					if (!tileValid) {
						continue;
					}
					std::uint32_t color = tilePixels.GetPixel(((px % 32) + 32) % 32, ((py % 32) + 32) % 32, palette);
					if ((tilePixels.Flags & Formats::TileRecord::ModifierMask) == Formats::TileRecord::ModifierTranslucent) {
						color = (color & 0x00FFFFFFu) | (((color >> 24) * 180 / 255) << 24);
					}
					std::uint32_t& pixel = accum[std::size_t(sy) * SampleW + std::size_t(sx)];
					pixel = Blend(pixel, color);
				}
			}
		}

		data.push_back(std::uint8_t(PreviewMaxWidth));
		data.push_back(std::uint8_t(PreviewMaxWidth >> 8));
		data.push_back(std::uint8_t(PreviewMaxHeight));
		data.push_back(std::uint8_t(PreviewMaxHeight >> 8));
		for (std::int32_t y = 0; y < PreviewMaxHeight; y++) {
			for (std::int32_t x = 0; x < PreviewMaxWidth; x++) {
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

	std::uint32_t LevelThumbnail::Blend(std::uint32_t dst, std::uint32_t src)
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
}

#endif
