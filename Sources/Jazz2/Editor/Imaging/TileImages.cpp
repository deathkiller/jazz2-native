#include "TileImages.h"

#if defined(WITH_EDITOR)

#include "PaletteMatch.h"
#include "Quantizer.h"

#include <algorithm>
#include <cstring>
#include <memory>

namespace Jazz2::Editor
{
	namespace
	{
		constexpr std::int32_t TileSize = Formats::TileSetFile::TileSize;
		constexpr std::int32_t PixelCount = TileSize * TileSize;
	}

	PngImage MakeTileSetSheet(const Formats::TileSetData& tileSet, const std::uint32_t* palette, std::int32_t columns)
	{
		columns = std::max(columns, 1);
		std::int32_t rows = std::max((tileSet.TileCount + columns - 1) / columns, 1);
		bool indexed = !tileSet.HasTrueColorTiles();

		PngImage image;
		image.Create(columns * TileSize, rows * TileSize, indexed);
		if (indexed) {
			std::memcpy(image.Palette, tileSet.Palette, sizeof(image.Palette));
			// Index 0 is transparent in every tile set, whatever its color
			image.Palette[0] &= 0x00FFFFFFu;
			image.PaletteSize = 256;
		}

		for (std::int32_t tile = 0; tile < tileSet.TileCount; tile++) {
			const std::uint8_t* source = &tileSet.Images[std::size_t(tile) * Formats::TileSetData::ImageBytesPerTile];
			std::int32_t originX = (tile % columns) * TileSize;
			std::int32_t originY = (tile / columns) * TileSize;
			bool trueColor = (tileSet.TrueColor[tile] != 0);
			for (std::int32_t y = 0; y < TileSize; y++) {
				for (std::int32_t x = 0; x < TileSize; x++) {
					std::int32_t i = y * TileSize + x;
					std::size_t pos = std::size_t(originY + y) * std::size_t(image.Width) + std::size_t(originX + x);
					if (indexed) {
						image.Pixels[pos] = source[i];
						continue;
					}
					std::uint8_t* out = &image.Pixels[pos * 4];
					if (trueColor) {
						std::memcpy(out, &source[i * 4], 4);
					} else if (source[i] != 0) {
						std::uint32_t color = palette[source[i]];
						out[0] = std::uint8_t(color);
						out[1] = std::uint8_t(color >> 8);
						out[2] = std::uint8_t(color >> 16);
						out[3] = 255;
					}
				}
			}
		}
		return image;
	}

	PngImage MakeTileSetMaskSheet(const Formats::TileSetData& tileSet, std::int32_t columns)
	{
		columns = std::max(columns, 1);
		std::int32_t rows = std::max((tileSet.TileCount + columns - 1) / columns, 1);
		PngImage image;
		image.Create(columns * TileSize, rows * TileSize, true);
		image.Palette[0] = 0x00000000u;
		image.Palette[1] = 0xFF000000u;
		image.PaletteSize = 2;
		for (std::int32_t tile = 0; tile < tileSet.TileCount; tile++) {
			const std::uint8_t* mask = &tileSet.Masks[std::size_t(tile) * Formats::TileSetData::MaskBytesPerTile];
			std::int32_t originX = (tile % columns) * TileSize;
			std::int32_t originY = (tile / columns) * TileSize;
			for (std::int32_t i = 0; i < PixelCount; i++) {
				if (((mask[i / 8] >> (i % 8)) & 1) != 0) {
					image.Pixels[std::size_t(originY + i / TileSize) * std::size_t(image.Width) + std::size_t(originX + i % TileSize)] = 1;
				}
			}
		}
		return image;
	}

	PngImage MakeTileImage(const std::uint8_t* indices, const std::uint32_t* palette)
	{
		PngImage image;
		image.Create(TileSize, TileSize, true);
		std::memcpy(image.Palette, palette, sizeof(image.Palette));
		image.Palette[0] &= 0x00FFFFFFu;
		image.PaletteSize = 256;
		std::memcpy(image.Pixels.data(), indices, PixelCount);
		return image;
	}

	bool ReadTileFromImage(const PngImage& image, std::int32_t x, std::int32_t y, const std::uint32_t* palette, std::uint8_t* indices)
	{
		// The palette of an indexed image that has the same colors as ours, so its indices can be kept
		bool samePalette = image.Indexed;
		for (std::int32_t i = 1; samePalette && i < image.PaletteSize; i++) {
			samePalette = ((image.Palette[i] & 0x00FFFFFFu) == (palette[i] & 0x00FFFFFFu) || (image.Palette[i] >> 24) == 0);
		}

		OkLab colors[256];
		bool colorsReady = false;
		bool changed = false;
		for (std::int32_t ty = 0; ty < TileSize; ty++) {
			for (std::int32_t tx = 0; tx < TileSize; tx++) {
				std::int32_t px = x + tx, py = y + ty;
				std::uint8_t& out = indices[ty * TileSize + tx];
				if (px < 0 || py < 0 || px >= image.Width || py >= image.Height) {
					out = 0;
					continue;
				}
				std::uint32_t color = image.GetColor(px, py);
				if ((color >> 24) < 128) {
					out = 0;
					continue;
				}
				if (samePalette) {
					out = image.Pixels[std::size_t(py) * std::size_t(image.Width) + std::size_t(px)];
					continue;
				}
				// The closest entry, as the colors look, an exact one is taken as it is
				std::int32_t exact = -1;
				for (std::int32_t i = 1; i < 256; i++) {
					if ((palette[i] & 0x00FFFFFFu) == (color & 0x00FFFFFFu)) {
						exact = i;
						break;
					}
				}
				if (exact >= 0) {
					out = std::uint8_t(exact);
					continue;
				}
				if (!colorsReady) {
					for (std::int32_t i = 1; i < 256; i++) {
						colors[i] = OkLab::FromPaletteEntry(palette[i]);
					}
					colorsReady = true;
				}
				OkLab target = OkLab::FromPaletteEntry(color);
				std::int32_t best = 1;
				float bestDistance = target.DistanceSquared(colors[1]);
				for (std::int32_t i = 2; i < 256; i++) {
					float distance = target.DistanceSquared(colors[i]);
					if (distance < bestDistance) {
						bestDistance = distance;
						best = i;
					}
				}
				out = std::uint8_t(best);
				changed = true;
			}
		}
		return changed;
	}

	bool SliceTileSet(const PngImage& image, const TileImportOptions& options, Formats::TileSetData& tileSet, TileImportResult& result)
	{
		std::int32_t columns = image.Width / TileSize;
		std::int32_t rows = image.Height / TileSize;
		std::int32_t cellCount = columns * rows;
		if (cellCount <= 0) {
			return false;
		}
		result = {};
		TileImportColors colors = options.Colors;
		if (colors == TileImportColors::OwnPalette && !image.Indexed) {
			colors = TileImportColors::TrueColor;
		}

		// The palette of the tile set, entry 0 is always transparent
		std::uint32_t palette[256] = {};
		if (colors == TileImportColors::OwnPalette) {
			std::memcpy(palette, image.Palette, sizeof(palette));
		} else if (options.Palette != nullptr) {
			std::memcpy(palette, options.Palette, sizeof(palette));
		}
		palette[0] &= 0x00FFFFFFu;

		if (colors == TileImportColors::Reduce) {
			// The free entries are made for the opaque pixels of the tiles, every other one is enough for a large image
			bool locked[256] = {};
			for (std::int32_t i = 0; i < 256; i++) {
				locked[i] = (i == 0 || (options.Locked != nullptr && options.Locked[i]));
			}
			SmallVector<std::uint32_t, 0> pixels;
			std::int32_t step = (std::int64_t(columns) * rows * PixelCount > (1 << 20) ? 2 : 1);
			for (std::int32_t y = 0; y < rows * TileSize; y += step) {
				for (std::int32_t x = 0; x < columns * TileSize; x += step) {
					std::uint32_t color = image.GetColor(x, y);
					if ((color >> 24) >= 128) {
						pixels.push_back(color);
					}
				}
			}
			result.ColorsAdded = BuildPalette(pixels, palette, locked);
		}

		// An opaque pixel of index 0 of the image becomes the closest other entry, 0 is transparent in tile sets
		std::int32_t opaqueZero = -1;
		if (colors == TileImportColors::OwnPalette && (image.Palette[0] >> 24) >= 128) {
			OkLab zero = OkLab::FromPaletteEntry(image.Palette[0]);
			float bestDistance = 0.0f;
			for (std::int32_t i = 1; i < std::max(image.PaletteSize, 2); i++) {
				float distance = zero.DistanceSquared(OkLab::FromPaletteEntry(image.Palette[i]));
				if (opaqueZero < 0 || distance < bestDistance) {
					opaqueZero = i;
					bestDistance = distance;
				}
			}
		}

		std::unique_ptr<PaletteMapper> mapper;
		if (colors == TileImportColors::MatchPalette || colors == TileImportColors::Reduce) {
			mapper = std::make_unique<PaletteMapper>(palette, options.Dither);
		}

		struct Cell
		{
			std::uint8_t Image[Formats::TileSetData::ImageBytesPerTile];
			std::uint8_t Mask[Formats::TileSetData::MaskBytesPerTile];
			bool TrueColor;
			bool Empty;
		};
		SmallVector<Cell, 0> cells;
		cells.resize(std::size_t(cellCount));
		bool colorsChanged = false;
		for (std::int32_t c = 0; c < cellCount; c++) {
			Cell& cell = cells[c];
			std::memset(&cell, 0, sizeof(cell));
			std::int32_t originX = (c % columns) * TileSize;
			std::int32_t originY = (c / columns) * TileSize;
			cell.TrueColor = (colors == TileImportColors::TrueColor);

			bool any = false;
			for (std::int32_t i = 0; i < PixelCount; i++) {
				std::int32_t x = originX + i % TileSize, y = originY + i / TileSize;
				std::uint32_t color = image.GetColor(x, y);
				bool opaque = ((color >> 24) >= 128);
				switch (colors) {
					case TileImportColors::OwnPalette: {
						std::uint8_t index = image.Pixels[std::size_t(y) * std::size_t(image.Width) + std::size_t(x)];
						if (!opaque) {
							index = 0;
						} else if (index == 0) {
							index = std::uint8_t(opaqueZero);
							colorsChanged = true;
						}
						cell.Image[i] = index;
						break;
					}
					case TileImportColors::TrueColor:
						// Byte by byte, the order of the bytes of a number differs between machines
						cell.Image[i * 4] = std::uint8_t(color);
						cell.Image[i * 4 + 1] = std::uint8_t(color >> 8);
						cell.Image[i * 4 + 2] = std::uint8_t(color >> 16);
						cell.Image[i * 4 + 3] = std::uint8_t(color >> 24);
						break;
					default:
						if (opaque) {
							// The pattern follows the pixels of the tile, so tiles next to each other continue it
							std::uint8_t index = mapper->Map(color, i % TileSize, i / TileSize);
							colorsChanged |= ((palette[index] & 0x00FFFFFFu) != (color & 0x00FFFFFFu));
							cell.Image[i] = index;
						}
						break;
				}
				any |= opaque;

				// Solid where the masks are, or where the tile isn't transparent
				bool solid = opaque;
				if (options.Masks != nullptr) {
					solid = (x < options.Masks->Width && y < options.Masks->Height && (options.Masks->GetColor(x, y) >> 24) >= 128);
				}
				if (solid) {
					cell.Mask[i / 8] |= std::uint8_t(1 << (i % 8));
					any = true;
				}
			}
			cell.Empty = !any;
		}

		// The empty tile first, the empty tiles at the end left out
		result.AddedEmptyTile = !cells[0].Empty;
		std::int32_t last = cellCount - 1;
		while (last > 0 && cells[last].Empty) {
			last--;
		}
		result.TrimmedTiles = cellCount - 1 - last;
		result.ColorsChanged = colorsChanged;

		// Tiles that are the same as one before them, the empty tile is always kept as the first one
		SmallVector<std::int32_t, 0> kept;
		for (std::int32_t c = 0; c <= last; c++) {
			bool duplicate = false;
			if (options.SkipDuplicates) {
				if (cells[c].Empty && (result.AddedEmptyTile || c > 0)) {
					duplicate = true;
				}
				for (std::size_t k = 0; !duplicate && k < kept.size(); k++) {
					const Cell& other = cells[kept[k]];
					duplicate = (other.TrueColor == cells[c].TrueColor && std::memcmp(other.Image, cells[c].Image, sizeof(other.Image)) == 0 &&
						std::memcmp(other.Mask, cells[c].Mask, sizeof(other.Mask)) == 0);
				}
			}
			if (duplicate) {
				result.DuplicateTiles++;
			} else {
				kept.push_back(c);
			}
		}

		std::int32_t tileCount = std::int32_t(kept.size()) + (result.AddedEmptyTile ? 1 : 0);
		tileSet.Resize(tileCount);
		std::memcpy(tileSet.Palette, palette, sizeof(palette));
		tileSet.Columns = 0;
		std::int32_t target = 0;
		if (result.AddedEmptyTile) {
			std::memset(&tileSet.Images[0], 0, Formats::TileSetData::ImageBytesPerTile);
			std::memset(&tileSet.Masks[0], 0, Formats::TileSetData::MaskBytesPerTile);
			tileSet.TrueColor[0] = 0;
			target = 1;
		}
		for (std::int32_t c : kept) {
			std::memcpy(&tileSet.Images[std::size_t(target) * Formats::TileSetData::ImageBytesPerTile], cells[c].Image, Formats::TileSetData::ImageBytesPerTile);
			std::memcpy(&tileSet.Masks[std::size_t(target) * Formats::TileSetData::MaskBytesPerTile], cells[c].Mask, Formats::TileSetData::MaskBytesPerTile);
			tileSet.TrueColor[target] = (cells[c].TrueColor ? 1 : 0);
			target++;
		}
		result.TileCount = tileCount;
		return true;
	}
}

#endif
