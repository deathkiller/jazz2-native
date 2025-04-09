#include "TileSet.h"

namespace Jazz2::Tiles
{
	TileSet::TileSet(std::uint16_t tileCount, std::unique_ptr<Texture> textureDiffuse, std::unique_ptr<uint8_t[]> mask, std::uint32_t maskSize, std::unique_ptr<Color[]> captionTile)
		: TextureDiffuse(std::move(textureDiffuse)), _mask(std::move(mask)), _captionTile(std::move(captionTile)),
			_isMaskEmpty(), _isMaskFilled(), _isTileFilled()
	{
		// TilesPerRow is used only for rendering
		if (TextureDiffuse != nullptr) {
			Vector2i texSize = TextureDiffuse->size();
			TilesPerRow = (texSize.X / (DefaultTileSize + 2));
		} else {
			TilesPerRow = 0;
		}

		TileCount = tileCount;
		_isMaskEmpty.resize(ValueInit, TileCount);
		_isMaskFilled.resize(ValueInit, TileCount);
		_isTileFilled.resize(ValueInit, TileCount);

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

			// TODO: _isTileFilled is not properly set
			if (/*tileFilled ||*/ !maskEmpty) {
				_isTileFilled.set(i);
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
		return TextureDiffuse->loadFromTexels((std::uint8_t*)tileDiffuse.data(), x, y, DefaultTileSize + 2, DefaultTileSize + 2);
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

		return true;
	}
}