#include "TileSet.h"

#include <cstring>

namespace Jazz2::Tiles
{
	TileSet::TileSet(StringView path, std::uint16_t tileCount, SmallVector<std::unique_ptr<Texture>, 1>&& textureDiffuse, std::unique_ptr<uint8_t[]> mask, std::uint32_t maskSize, std::unique_ptr<Color[]> captionTile, const std::uint8_t* tileDiffuseOpaque)
		: FilePath(path), TextureDiffuse(std::move(textureDiffuse)), _mask(std::move(mask)), _captionTile(std::move(captionTile)),
			_isMaskEmpty(), _isMaskFilled(), _isTileFilled(), _isColumnContiguous()
	{
		// TilesPerRow/TilesPerTexture are used only for rendering. Every chunk shares the layout of chunk 0
		// (the last one may be shorter), so its size defines how many tiles each chunk covers.
		if (!TextureDiffuse.empty() && TextureDiffuse[0] != nullptr) {
			Vector2i texSize = TextureDiffuse[0]->GetSize();
			TilesPerRow = (texSize.X / (DefaultTileSize + 2));
			TilesPerTexture = TilesPerRow * (texSize.Y / (DefaultTileSize + 2));
		} else {
			TilesPerRow = 0;
			TilesPerTexture = 0;
		}

		TileCount = tileCount;
		_isMaskEmpty.resize(ValueInit, TileCount);
		_isMaskFilled.resize(ValueInit, TileCount);
		_isTileFilled.resize(ValueInit, TileCount);
		_isColumnContiguous.resize(ValueInit, TileCount);
		// 2 bytes per column (first/last solid row); zero-initialized by make_unique
		_columnSpans = std::make_unique<std::uint8_t[]>((std::size_t)TileCount * DefaultTileSize * 2);

		std::uint32_t maskMaxTiles = maskSize / MaskBytesPerTile;

		for (std::uint32_t i = 0; i < tileCount; i++) {
			bool maskEmpty = true;
			bool maskFilled = true;

			if (i < maskMaxTiles) {
				// The mask is packed (1 bit per pixel, the cache file format), so empty/filled collapse
				// to byte compares over the tile's 128 bytes
				auto* maskOffset = &_mask[i * MaskBytesPerTile];
				for (std::int32_t j = 0; j < MaskBytesPerTile; j++) {
					maskEmpty &= (maskOffset[j] == 0x00);
					maskFilled &= (maskOffset[j] == 0xFF);
				}
			}

			if (maskEmpty) {
				_isMaskEmpty.set(i);
			}
			if (maskFilled) {
				_isMaskFilled.set(i);
			}

			// A tile is "filled" for rendering when its diffuse is fully opaque (used to cull hidden debris).
			// The flag is computed from the diffuse alpha by the content loader; it is absent in headless
			// mode, where rendering - and therefore this optimization - does not run.
			if (tileDiffuseOpaque != nullptr && tileDiffuseOpaque[i] != 0) {
				_isTileFilled.set(i);
			}

			// Precompute per-column solid spans. A tile whose every column is vertically contiguous
			// (no solid-empty-solid gaps) can answer "is any pixel in this sub-rectangle solid?" with
			// an exact per-column span overlap test, avoiding the O(width*height) per-pixel scan.
			// Fully filled tiles are handled by an earlier IsTileMaskFilled() early-out, and empty
			// tiles by IsTileMaskEmpty(), so this mainly accelerates slopes and other partial tiles.
			std::uint8_t* spans = &_columnSpans[(std::size_t)i * DefaultTileSize * 2];
			bool columnContiguous = (i < maskMaxTiles && !maskEmpty);
			if (columnContiguous) {
				// Load each packed row once as a 32-bit word, then walk columns as bit tests
				const std::uint8_t* maskOffset = &_mask[i * MaskBytesPerTile];
				std::uint32_t rows[DefaultTileSize];
				for (std::int32_t row = 0; row < DefaultTileSize; row++) {
					rows[row] = GetTileMaskRow(maskOffset, row);
				}
				for (std::int32_t col = 0; col < DefaultTileSize; col++) {
					std::int32_t firstSolid = -1, lastSolid = -1, solidCount = 0;
					for (std::int32_t row = 0; row < DefaultTileSize; row++) {
						if ((rows[row] >> col) & 1) {
							if (firstSolid < 0) {
								firstSolid = row;
							}
							lastSolid = row;
							solidCount++;
						}
					}
					if (firstSolid < 0) {
						spans[col * 2] = 0xFF; // Empty column (0xFF first row never satisfies "<= bottom")
						spans[col * 2 + 1] = 0xFF;
					} else {
						spans[col * 2] = (std::uint8_t)firstSolid;
						spans[col * 2 + 1] = (std::uint8_t)lastSolid;
						if ((lastSolid - firstSolid + 1) != solidCount) {
							// Column has a gap, span would over-report; fall back to per-pixel scan
							columnContiguous = false;
						}
					}
				}
			}
			if (columnContiguous) {
				_isColumnContiguous.set(i);
			}
		}
	}

	bool TileSet::OverrideTileDiffuse(std::int32_t tileId, StaticArrayView<(DefaultTileSize + 2) * (DefaultTileSize + 2), std::uint32_t> tileDiffuse)
	{
		if (tileId >= TileCount) {
			return false;
		}

		// The tile may live in any texture chunk when the atlas was split by the device texture-size limit
		std::int32_t localTileId = tileId;
		Texture* texture = ResolveTextureDiffuse(localTileId);
		if (texture == nullptr) {
			return false;
		}

		std::int32_t x = (localTileId % TilesPerRow) * (DefaultTileSize + 2);
		std::int32_t y = (localTileId / TilesPerRow) * (DefaultTileSize + 2);

		// The incoming tile is RGBA (palette index in red, alpha in alpha). Repack it to match the atlas format,
		// which may have been reduced to R8 (index only) or RG8 (index + alpha) to save VRAM (see CreateIndexedTexture)
		constexpr std::int32_t Count = (DefaultTileSize + 2) * (DefaultTileSize + 2);
		std::uint32_t channels = texture->GetChannelCount();

		// A live tile edit also refreshes the "fully filled" flag, which matches BuildTilesetDiffuse's
		// definition: no transparent texel in the 32x32 interior (the 1px ring is padding). For an indexed
		// atlas that means no index-0 texel - index 0 samples the palette's transparent base entry - so it
		// is what the flag's consumers combine with the palette's own alpha; an RG8 or baked atlas
		// additionally carries per-texel alpha, which must be a full 255 everywhere.
		bool filled = true;
		for (std::int32_t py = 1; py <= DefaultTileSize && filled; py++) {
			for (std::int32_t px = 1; px <= DefaultTileSize; px++) {
				std::uint32_t c = tileDiffuse[py * (DefaultTileSize + 2) + px];
				const std::uint32_t alpha = (c >> 24) & 0xFF;
				const bool opaquePx = (channels == 1
					? (alpha != 0 && (c & 0xFF) != 0)
					: (channels == 2
						? (alpha == 255 && (c & 0xFF) != 0)
						: (alpha == 255)));
				if (!opaquePx) {
					filled = false;
					break;
				}
			}
		}

		bool result;
		if (channels == 1) {
			std::uint8_t packed[Count];
			for (std::int32_t i = 0; i < Count; i++) {
				std::uint32_t c = tileDiffuse[i];
				packed[i] = (((c >> 24) & 0xFF) == 0 ? 0 : (std::uint8_t)(c & 0xFF));
			}
			result = texture->LoadFromTexels(packed, x, y, DefaultTileSize + 2, DefaultTileSize + 2);
		} else if (channels == 2) {
			std::uint8_t packed[Count * 2];
			for (std::int32_t i = 0; i < Count; i++) {
				std::uint32_t c = tileDiffuse[i];
				packed[(i * 2) + 0] = (std::uint8_t)(c & 0xFF);
				packed[(i * 2) + 1] = (std::uint8_t)((c >> 24) & 0xFF);
			}
			result = texture->LoadFromTexels(packed, x, y, DefaultTileSize + 2, DefaultTileSize + 2);
		} else {
			result = texture->LoadFromTexels((std::uint8_t*)tileDiffuse.data(), x, y, DefaultTileSize + 2, DefaultTileSize + 2);
		}
		if (result) {
			_isTileFilled.set(tileId, filled);
		}
		return result;
	}

	bool TileSet::OverrideTileMask(std::int32_t tileId, StaticArrayView<DefaultTileSize * DefaultTileSize, std::uint8_t> tileMask)
	{
		if (tileId >= TileCount) {
			return false;
		}

		// The level cache delivers overridden masks byte-per-pixel; pack them into the tile's 1-bit store
		auto* maskOffset = &_mask[tileId * MaskBytesPerTile];
		std::memset(maskOffset, 0, MaskBytesPerTile);

		bool maskEmpty = true;
		bool maskFilled = true;
		for (std::int32_t j = 0; j < DefaultTileSize * DefaultTileSize; j++) {
			bool masked = (tileMask[j] > 0);
			if (masked) {
				maskOffset[j >> 3] |= std::uint8_t(1u << (j & 7));
			}
			maskEmpty &= !masked;
			maskFilled &= masked;
		}

		_isMaskEmpty.set(tileId, maskEmpty);
		_isMaskFilled.set(tileId, maskFilled);
		// Disable the column-span fast path for overridden masks, the per-pixel scan stays correct
		_isColumnContiguous.set(tileId, false);

		return true;
	}
}