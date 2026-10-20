#include "TileImages.h"

#if defined(WITH_EDITOR)

#include "PaletteMatch.h"
#include "Quantizer.h"
#include "TileMasks.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <unordered_map>

#include <Cryptography/xxHash.h>

using Death::Cryptography::xxHash3;
using nCine::Vector2i;

namespace Jazz2::Editor
{
	namespace
	{
		constexpr std::int32_t TileSize = Formats::TileSetFile::TileSize;
		constexpr std::int32_t PixelCount = TileSize * TileSize;

		// A hash of the pixels and the mask of a tile
		std::uint64_t HashTile(const std::uint8_t* image, std::size_t imageBytes, const std::uint8_t* mask)
		{
			std::uint64_t hash = xxHash3(image, imageBytes);
			return xxHash3(mask, Formats::TileSetData::MaskBytesPerTile, hash);
		}
	}

	PngImage MakeTileSetSheet(const Formats::TileSetData& tileSet, const std::uint32_t* palette, std::int32_t columns)
	{
		columns = std::max<std::int32_t>(columns, 1);
		std::int32_t rows = std::max<std::int32_t>((tileSet.TileCount + columns - 1) / columns, 1);
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
		columns = std::max<std::int32_t>(columns, 1);
		std::int32_t rows = std::max<std::int32_t>((tileSet.TileCount + columns - 1) / columns, 1);
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
				if (TileMasks::GetBit(mask, i)) {
					image.Pixels[std::size_t(originY + i / TileSize) * std::size_t(image.Width) + std::size_t(originX + i % TileSize)] = 1;
				}
			}
		}
		return image;
	}

	std::uint64_t HashTileOf(const Formats::TileSetData& data, std::int32_t tile)
	{
		std::size_t imageBytes = (data.TrueColor[tile] != 0 ? std::size_t(Formats::TileSetData::ImageBytesPerTile) : std::size_t(PixelCount));
		std::uint64_t hash = xxHash3(&data.Images[std::size_t(tile) * Formats::TileSetData::ImageBytesPerTile], imageBytes);
		hash = xxHash3(&data.Masks[std::size_t(tile) * Formats::TileSetData::MaskBytesPerTile], Formats::TileSetData::MaskBytesPerTile, hash);
		return hash ^ data.TrueColor[tile];
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

		// Made only once a color isn't in the palette
		std::unique_ptr<PaletteSearch> search;
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
				if (search == nullptr) {
					search = std::make_unique<PaletteSearch>(palette);
				}
				out = search->FindNearest(OkLab::FromPaletteEntry(color));
				changed = true;
			}
		}
		return changed;
	}

	PngImage MakeTextureImage(const std::uint8_t* indices, std::int32_t size, const std::uint32_t* palette)
	{
		PngImage image;
		image.Create(size, size, true);
		std::memcpy(image.Palette, palette, sizeof(image.Palette));
		image.Palette[0] &= 0x00FFFFFFu;
		image.PaletteSize = 256;
		std::memcpy(image.Pixels.data(), indices, std::size_t(size) * std::size_t(size));
		return image;
	}

	bool ReadTextureFromImage(const PngImage& image, std::int32_t size, const std::uint32_t* palette, std::uint8_t* indices)
	{
		// Tile by tile, each read the same way as a tile of a tile set
		bool changed = false;
		std::uint8_t tile[PixelCount];
		for (std::int32_t ty = 0; ty < size; ty += TileSize) {
			for (std::int32_t tx = 0; tx < size; tx += TileSize) {
				changed |= ReadTileFromImage(image, tx, ty, palette, tile);
				for (std::int32_t y = 0; y < TileSize && ty + y < size; y++) {
					std::memcpy(&indices[std::size_t(ty + y) * std::size_t(size) + std::size_t(tx)], &tile[y * TileSize],
						std::size_t(std::min<std::int32_t>(TileSize, size - tx)));
				}
			}
		}
		return changed;
	}

	namespace
	{
		// The layout of the tiles in an image, with a margin and gutters as sprite sheets may have them
		struct SheetLayout
		{
			std::int32_t OffsetX, OffsetY, StepX, StepY, Columns, Rows;

			SheetLayout(const PngImage& image, const TileImportOptions& options)
			{
				// Nothing larger than the image itself makes sense, and it keeps the arithmetic small
				OffsetX = std::clamp<std::int32_t>(options.OffsetX, 0, image.Width);
				OffsetY = std::clamp<std::int32_t>(options.OffsetY, 0, image.Height);
				StepX = TileSize + std::clamp<std::int32_t>(options.SpacingX, 0, image.Width);
				StepY = TileSize + std::clamp<std::int32_t>(options.SpacingY, 0, image.Height);
				// The last tile needs no gutter after it
				Columns = (image.Width - OffsetX >= TileSize ? (image.Width - OffsetX - TileSize) / StepX + 1 : 0);
				Rows = (image.Height - OffsetY >= TileSize ? (image.Height - OffsetY - TileSize) / StepY + 1 : 0);
			}

			Vector2i GetOrigin(std::int32_t cell) const
			{
				return Vector2i(OffsetX + (cell % Columns) * StepX, OffsetY + (cell / Columns) * StepY);
			}

			// Whether a pixel of the image is in a tile, not in the margin or a gutter
			bool Contains(std::int32_t x, std::int32_t y) const
			{
				std::int32_t dx = x - OffsetX, dy = y - OffsetY;
				return (dx >= 0 && dy >= 0 && dx / StepX < Columns && dy / StepY < Rows && dx % StepX < TileSize && dy % StepY < TileSize);
			}
		};
	}

	Vector2i GetTileGrid(const PngImage& image, const TileImportOptions& options)
	{
		SheetLayout grid(image, options);
		return Vector2i(grid.Columns, grid.Rows);
	}

	bool SliceTileSet(const PngImage& image, const TileImportOptions& options, Formats::TileSetData& tileSet, TileImportResult& result)
	{
		result = {};
		SheetLayout grid(image, options);
		std::int32_t columns = grid.Columns;
		std::int32_t rows = grid.Rows;
		std::int64_t cellCount64 = std::int64_t(columns) * rows;
		if (cellCount64 <= 0) {
			result.Error = TileImportError::TooSmall;
			return false;
		}
		std::int32_t cellCount = std::int32_t(cellCount64);
		TileImportColors colors = options.Colors;
		if (colors == TileImportColors::OwnPalette && !image.Indexed) {
			colors = TileImportColors::TrueColor;
		}
		auto cancelled = [&tileSet, &result]() {
			tileSet.Resize(0);
			result = {};
			result.Error = TileImportError::Cancelled;
			return false;
		};
		auto report = [&options](float value) {
			return (!options.Progress || options.Progress(value));
		};
		// Which pixels can be seen, by their alpha, and which are of the color that stands for transparent ones
		std::uint32_t alphaThreshold = std::max<std::uint32_t>(options.AlphaThreshold, 1);
		auto isKeyed = [&options](std::uint32_t color) {
			return (options.UseColorKey && (color & 0x00FFFFFFu) == (options.ColorKey & 0x00FFFFFFu));
		};
		auto isOpaque = [alphaThreshold, &isKeyed](std::uint32_t color) {
			return ((color >> 24) >= alphaThreshold && !isKeyed(color));
		};
		// The quantizing takes most of the time for a large image, slicing the rest
		bool reduce = (colors == TileImportColors::Reduce);
		bool diffuse = (options.ErrorDiffusion && (colors == TileImportColors::MatchPalette || reduce));
		float reduceEnd = (reduce ? 0.4f : 0.0f);
		float sliceStart = reduceEnd + (diffuse ? 0.3f : 0.0f);

		// The palette of the tile set, entry 0 is always transparent
		std::uint32_t palette[256] = {};
		if (colors == TileImportColors::OwnPalette) {
			std::memcpy(palette, image.Palette, sizeof(palette));
			// The transparency of the image's entries decides only which pixels are drawn, the tiles keep that as index
			// 0. The palette of a tile set is opaque, it becomes the palette of the level, which the sprites share.
			for (std::int32_t i = 1; i < 256; i++) {
				palette[i] |= 0xFF000000u;
			}
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
			for (std::int32_t c = 0; c < cellCount; c++) {
				Vector2i origin = grid.GetOrigin(c);
				for (std::int32_t y = 0; y < TileSize; y += step) {
					for (std::int32_t x = 0; x < TileSize; x += step) {
						std::uint32_t color = image.GetColor(origin.X + x, origin.Y + y);
						if (isOpaque(color)) {
							pixels.push_back(color);
						}
					}
				}
			}
			std::int32_t added = BuildPalette(pixels, palette, locked, [&report, reduceEnd](float value) {
				return report(value * reduceEnd);
			});
			if (added < 0) {
				return cancelled();
			}
			result.ColorsAdded = added;
		}

		// An opaque pixel of index 0 of the image becomes the closest other entry, 0 is transparent in tile sets. Opaque
		// is by the same threshold as the pixels, so every such pixel has an entry to become.
		std::int32_t opaqueZero = -1;
		if (colors == TileImportColors::OwnPalette && isOpaque(image.Palette[0])) {
			OkLab zero = OkLab::FromPaletteEntry(image.Palette[0]);
			float bestDistance = 0.0f;
			for (std::int32_t i = 1; i < std::max<std::int32_t>(image.PaletteSize, 2); i++) {
				float distance = zero.DistanceSquared(OkLab::FromPaletteEntry(image.Palette[i]));
				if (opaqueZero < 0 || distance < bestDistance) {
					opaqueZero = i;
					bestDistance = distance;
				}
			}
		}

		std::unique_ptr<PaletteMapper> mapper;
		if (colors == TileImportColors::MatchPalette || colors == TileImportColors::Reduce) {
			mapper = std::make_unique<PaletteMapper>(palette, options.Dither && !diffuse);
		}

		// With error diffusion, the whole image is matched to the palette first, row by row, the error of each pixel goes
		// to the pixels right of it and below it that are in tiles and can be seen. The tiles then take the indices.
		SmallVector<std::uint8_t, 0> diffused;
		bool diffusedChanged = false;
		if (diffuse) {
			PaletteSearch search(palette);
			diffused.resize(std::size_t(image.Width) * std::size_t(image.Height), 0);
			// The error of this row and of the next one, RGB, one pixel more on each side for the edges
			SmallVector<float, 0> errors[2];
			errors[0].resize(std::size_t(image.Width + 2) * 3, 0.0f);
			errors[1].resize(std::size_t(image.Width + 2) * 3, 0.0f);
			std::int32_t lastRow = std::min<std::int32_t>(image.Height, grid.OffsetY + (rows - 1) * grid.StepY + TileSize);
			for (std::int32_t y = grid.OffsetY; y < lastRow; y++) {
				SmallVector<float, 0>& current = errors[y & 1];
				SmallVector<float, 0>& next = errors[(y + 1) & 1];
				std::fill(next.begin(), next.end(), 0.0f);
				for (std::int32_t x = 0; x < image.Width; x++) {
					std::uint32_t color = image.GetColor(x, y);
					float* error = &current[std::size_t(x + 1) * 3];
					if (!grid.Contains(x, y) || !isOpaque(color)) {
						// Transparent pixels and gutters don't take any error, nor pass it on
						continue;
					}
					float want[3];
					for (std::int32_t ch = 0; ch < 3; ch++) {
						want[ch] = std::clamp(float((color >> (ch * 8)) & 0xFF) + error[ch], 0.0f, 255.0f);
					}
					std::uint32_t wanted = std::uint32_t(want[0] + 0.5f) | (std::uint32_t(want[1] + 0.5f) << 8) | (std::uint32_t(want[2] + 0.5f) << 16);
					std::uint8_t index = search.FindNearest(OkLab::FromPaletteEntry(wanted));
					diffused[std::size_t(y) * std::size_t(image.Width) + std::size_t(x)] = index;
					diffusedChanged |= ((palette[index] & 0x00FFFFFFu) != (color & 0x00FFFFFFu));
					for (std::int32_t ch = 0; ch < 3; ch++) {
						float rest = want[ch] - float((palette[index] >> (ch * 8)) & 0xFF);
						error[3 + ch] += rest * (7.0f / 16.0f);
						next[std::size_t(x) * 3 + ch] += rest * (3.0f / 16.0f);
						next[std::size_t(x + 1) * 3 + ch] += rest * (5.0f / 16.0f);
						next[std::size_t(x + 2) * 3 + ch] += rest * (1.0f / 16.0f);
					}
				}
				if ((y & 31) == 0 && !report(reduceEnd + (sliceStart - reduceEnd) * float(y - grid.OffsetY) / float(std::max<std::int32_t>(lastRow - grid.OffsetY, 1)))) {
					return cancelled();
				}
			}
		}

		// Tiles go into the tile set as they're sliced, only one cell of the image is held at a time. A tile that is the
		// same as an earlier one is found by a hash of its pixels and mask instead of comparing it with all of them.
		// Empty cells are only counted until a cell that isn't empty follows, the empty ones at the end are left out.
		struct Cell
		{
			std::uint8_t Image[Formats::TileSetData::ImageBytesPerTile];
			std::uint8_t Mask[Formats::TileSetData::MaskBytesPerTile];
			bool TrueColor;
			bool Empty;
		};
		bool trueColorTiles = (colors == TileImportColors::TrueColor);
		// An 8-bit tile takes the first quarter of its slot, the rest stays zero
		std::size_t imageBytes = (trueColorTiles ? std::size_t(Formats::TileSetData::ImageBytesPerTile) : std::size_t(PixelCount));
		auto cellPtr = std::make_unique<Cell>();
		Cell& cell = *cellPtr;
		std::unordered_multimap<std::uint64_t, std::int32_t> tileHashes;
		std::int32_t pendingEmpty = 0;

		tileSet.Resize(0);
		std::memcpy(tileSet.Palette, palette, sizeof(palette));
		tileSet.Columns = 0;
		auto appendTile = [&tileSet, imageBytes](const std::uint8_t* image, const std::uint8_t* mask, bool trueColor) {
			if (tileSet.TileCount >= MaxImportedTiles) {
				return false;
			}
			std::int32_t index = tileSet.TileCount;
			tileSet.Resize(index + 1);
			if (image != nullptr) {
				std::memcpy(&tileSet.Images[std::size_t(index) * Formats::TileSetData::ImageBytesPerTile], image, imageBytes);
				std::memcpy(&tileSet.Masks[std::size_t(index) * Formats::TileSetData::MaskBytesPerTile], mask, Formats::TileSetData::MaskBytesPerTile);
			}
			tileSet.TrueColor[index] = (trueColor ? 1 : 0);
			return true;
		};
		auto tooManyTiles = [&tileSet, &result]() {
			tileSet.Resize(0);
			result = {};
			result.Error = TileImportError::TooManyTiles;
			return false;
		};

		bool colorsChanged = diffusedChanged;
		for (std::int32_t c = 0; c < cellCount; c++) {
			if ((c & 255) == 0 && !report(sliceStart + (1.0f - sliceStart) * float(c) / float(cellCount))) {
				return cancelled();
			}
			std::memset(&cell, 0, sizeof(cell));
			Vector2i origin = grid.GetOrigin(c);
			std::int32_t originX = origin.X;
			std::int32_t originY = origin.Y;
			cell.TrueColor = trueColorTiles;

			bool any = false;
			for (std::int32_t i = 0; i < PixelCount; i++) {
				std::int32_t x = originX + i % TileSize, y = originY + i / TileSize;
				std::uint32_t color = image.GetColor(x, y);
				bool opaque = isOpaque(color);
				// A true color tile shows its translucent pixels too, it isn't empty with only those
				bool visible = (trueColorTiles ? ((color >> 24) != 0 && !isKeyed(color)) : opaque);
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
						// Byte by byte, the order of the bytes of a number differs between machines. The color of a pixel
						// that can't be seen is left zero, so tiles that look the same are the same.
						if (visible) {
							cell.Image[i * 4] = std::uint8_t(color);
							cell.Image[i * 4 + 1] = std::uint8_t(color >> 8);
							cell.Image[i * 4 + 2] = std::uint8_t(color >> 16);
							cell.Image[i * 4 + 3] = std::uint8_t(color >> 24);
						}
						break;
					default:
						if (opaque) {
							if (diffuse) {
								cell.Image[i] = diffused[std::size_t(y) * std::size_t(image.Width) + std::size_t(x)];
								break;
							}
							// The pattern follows the pixels of the tile, so tiles next to each other continue it
							std::uint8_t index = mapper->Map(color, i % TileSize, i / TileSize);
							colorsChanged |= ((palette[index] & 0x00FFFFFFu) != (color & 0x00FFFFFFu));
							cell.Image[i] = index;
						}
						break;
				}
				any |= visible;

				// Solid where the masks are, or where the tile isn't transparent
				bool solid = opaque;
				if (options.Masks != nullptr) {
					solid = (x < options.Masks->Width && y < options.Masks->Height && isOpaque(options.Masks->GetColor(x, y)));
				}
				if (solid) {
					TileMasks::SetBit(cell.Mask, i);
					any = true;
				}
			}
			cell.Empty = !any;

			if (c == 0) {
				// The empty tile is the first one, an empty one is put first if the image doesn't start with one
				result.AddedEmptyTile = !cell.Empty;
				if (cell.Empty) {
					appendTile(cell.Image, cell.Mask, cell.TrueColor);
					continue;
				}
				appendTile(nullptr, nullptr, false);
			}
			if (cell.Empty) {
				pendingEmpty++;
				continue;
			}

			// Empty cells with a tile after them aren't at the end, they're the same as the empty tile
			if (pendingEmpty > 0) {
				if (options.SkipDuplicates) {
					result.DuplicateTiles += pendingEmpty;
				} else {
					for (std::int32_t i = 0; i < pendingEmpty; i++) {
						if (!appendTile(nullptr, nullptr, trueColorTiles)) {
							return tooManyTiles();
						}
					}
				}
				pendingEmpty = 0;
			}

			if (options.SkipDuplicates) {
				std::uint64_t hash = HashTile(cell.Image, imageBytes, cell.Mask);
				bool duplicate = false;
				auto range = tileHashes.equal_range(hash);
				for (auto it = range.first; it != range.second && !duplicate; ++it) {
					std::int32_t other = it->second;
					duplicate = ((tileSet.TrueColor[other] != 0) == cell.TrueColor &&
						std::memcmp(&tileSet.Images[std::size_t(other) * Formats::TileSetData::ImageBytesPerTile], cell.Image, imageBytes) == 0 &&
						std::memcmp(&tileSet.Masks[std::size_t(other) * Formats::TileSetData::MaskBytesPerTile], cell.Mask, Formats::TileSetData::MaskBytesPerTile) == 0);
				}
				if (duplicate) {
					result.DuplicateTiles++;
					continue;
				}
				tileHashes.emplace(hash, tileSet.TileCount);
			}
			if (!appendTile(cell.Image, cell.Mask, cell.TrueColor)) {
				return tooManyTiles();
			}
		}

		result.TrimmedTiles = pendingEmpty;
		result.ColorsChanged = colorsChanged;
		result.TileCount = tileSet.TileCount;
		report(1.0f);
		return true;
	}
}

#endif
