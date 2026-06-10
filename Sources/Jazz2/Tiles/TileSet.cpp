#include "TileSet.h"

namespace Jazz2::Tiles
{
	TileSet::TileSet(StringView path, std::uint16_t tileCount, std::unique_ptr<Texture> textureDiffuse, std::unique_ptr<uint8_t[]> mask, std::uint32_t maskSize, std::unique_ptr<Color[]> captionTile, const std::uint8_t* tileDiffuseOpaque)
		: FilePath(path), TextureDiffuse(std::move(textureDiffuse)), _mask(std::move(mask)), _captionTile(std::move(captionTile)),
			_isMaskEmpty(), _isMaskFilled(), _isTileFilled(), _isColumnContiguous()
	{
		// TilesPerRow is used only for rendering
		if (TextureDiffuse != nullptr) {
			Vector2i texSize = TextureDiffuse->GetSize();
			TilesPerRow = (texSize.X / (DefaultTileSize + 2));
		} else {
			TilesPerRow = 0;
		}

		TileCount = tileCount;
		_isMaskEmpty.resize(ValueInit, TileCount);
		_isMaskFilled.resize(ValueInit, TileCount);
		_isTileFilled.resize(ValueInit, TileCount);
		_isColumnContiguous.resize(ValueInit, TileCount);
		// 2 bytes per column (first/last solid row); zero-initialized by make_unique
		_columnSpans = std::make_unique<std::uint8_t[]>((std::size_t)TileCount * DefaultTileSize * 2);

		std::uint32_t maskMaxTiles = maskSize / (DefaultTileSize * DefaultTileSize);

		for (std::uint32_t i = 0; i < tileCount; i++) {
			bool maskEmpty = true;
			bool maskFilled = true;

			if (i < maskMaxTiles) {
				auto* maskOffset = &_mask[i * DefaultTileSize * DefaultTileSize];
				for (std::int32_t j = 0; j < DefaultTileSize * DefaultTileSize; j++) {
					bool masked = (maskOffset[j] > 0);
					maskEmpty &= !masked;
					maskFilled &= masked;
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
				auto* maskOffset = &_mask[i * DefaultTileSize * DefaultTileSize];
				for (std::int32_t col = 0; col < DefaultTileSize; col++) {
					std::int32_t firstSolid = -1, lastSolid = -1, solidCount = 0;
					for (std::int32_t row = 0; row < DefaultTileSize; row++) {
						if (maskOffset[row * DefaultTileSize + col] > 0) {
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

		std::int32_t x = (tileId % TilesPerRow) * (DefaultTileSize + 2);
		std::int32_t y = (tileId / TilesPerRow) * (DefaultTileSize + 2);

		// TODO: _isTileFilled is not properly set
		return TextureDiffuse->LoadFromTexels((std::uint8_t*)tileDiffuse.data(), x, y, DefaultTileSize + 2, DefaultTileSize + 2);
	}

	bool TileSet::OverrideTileMask(std::int32_t tileId, StaticArrayView<DefaultTileSize * DefaultTileSize, std::uint8_t> tileMask)
	{
		if (tileId >= TileCount) {
			return false;
		}

		auto* maskOffset = &_mask[tileId * DefaultTileSize * DefaultTileSize];

		bool maskEmpty = true;
		bool maskFilled = true;
		for (std::int32_t j = 0; j < DefaultTileSize * DefaultTileSize; j++) {
			maskOffset[j] = tileMask[j];
			bool masked = (tileMask[j] > 0);
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