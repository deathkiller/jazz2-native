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
		_isMaskEmpty.SetSize(TileCount);
		_isMaskFilled.SetSize(TileCount);
		_isTileFilled.SetSize(TileCount);

		std::uint32_t maskMaxTiles = maskSize / (DefaultTileSize * DefaultTileSize);

		for (std::uint32_t i = 0; i < tileCount; i++) {
			bool maskEmpty = true;
			bool maskFilled = true;

			if (i < maskMaxTiles) {
				auto maskOffset = &_mask[i * DefaultTileSize * DefaultTileSize];
				for (std::int32_t x = 0; x < DefaultTileSize * DefaultTileSize; x++) {
					bool masked = (maskOffset[x] > 0);
					maskEmpty &= !masked;
					maskFilled &= masked;
				}
			}

			if (maskEmpty) {
				_isMaskEmpty.Set(i);
			}
			if (maskFilled) {
				_isMaskFilled.Set(i);
			}
			if (/*tileFilled ||*/ !maskEmpty) {
				_isTileFilled.Set(i);
			}
		}
	}
}