#include "TileSet.h"

namespace Jazz2::Tiles
{
	TileSet::TileSet(std::unique_ptr<Texture> textureDiffuse, std::unique_ptr<uint8_t[]> mask, uint32_t maskSize, std::unique_ptr<Color[]> captionTile)
		:
		TextureDiffuse(std::move(textureDiffuse)),
		_mask(std::move(mask)),
		_captionTile(std::move(captionTile)),
		_isMaskEmpty(),
		_isMaskFilled(),
		_isTileFilled()
	{
		Vector2i texSize = TextureDiffuse->size();
		int tw = (texSize.X / DefaultTileSize);
		int th = (texSize.Y / DefaultTileSize);

		TileCount = tw * th;
		TilesPerRow = tw;
		_isMaskEmpty.SetSize(TileCount);
		_isMaskFilled.SetSize(TileCount);
		_isTileFilled.SetSize(TileCount);

		//_defaultLayerTiles.reserve(_tileCount);
		uint32_t maskMaxTiles = maskSize / (DefaultTileSize * DefaultTileSize);

		int k = 0;
		for (int i = 0; i < th; i++) {
			for (int j = 0; j < tw; j++) {
				bool maskEmpty = true;
				bool maskFilled = true;
				// TODO
				//bool tileFilled = true;

				//auto pixelOffset = &pixels[(i * Tiles::TileSet::DefaultTileSize * w) + (j * Tiles::TileSet::DefaultTileSize)];
				if (k < maskMaxTiles) {
					auto maskOffset = &_mask[k * DefaultTileSize * DefaultTileSize];
					for (int x = 0; x < DefaultTileSize * DefaultTileSize; x++) {
						bool masked = (maskOffset[x] > 0);
						maskEmpty &= !masked;
						maskFilled &= masked;

						//ColorRgba pxTex = texture[j * DefaultTileSize + x, i * DefaultTileSize + y];
						//masked = (pxTex.A > 20);
						//tileFilled &= masked;
					}
				}

				int idx = (j + tw * i);
				if (maskEmpty) {
					_isMaskEmpty.Set(idx);
				}
				if (maskFilled) {
					_isMaskFilled.Set(idx);
				}
				if (/*tileFilled ||*/ !maskEmpty) {
					_isTileFilled.Set(idx);
				}

				/*defaultLayerTiles.Add(LayerTile {
					TileID = idx,

					Material = material,
					MaterialOffset = new Point2(TileSize * ((i * width + j) % TilesPerRow), TileSize * ((i * width + j) / TilesPerRow)),
					MaterialAlpha = 255
				});*/

				k++;
			}
		}
	}
}