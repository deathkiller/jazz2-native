#include "TileSet.h"

namespace Jazz2::Tiles
{
	TileSet::TileSet(std::unique_ptr<Texture> textureDiffuse, std::unique_ptr<uint8_t[]> mask)
		:
		_textureDiffuse(std::move(textureDiffuse)),
		_mask(std::move(mask)),
		_isMaskEmpty(),
		_isMaskFilled(),
		_isTileFilled()
	{
		auto texSize = _textureDiffuse->size();

		int tw = (texSize.X / DefaultTileSize);
		int th = (texSize.Y / DefaultTileSize);

		_tileCount = tw * th;
		_tilesPerRow = tw;
		_isMaskEmpty.SetSize(_tileCount);
		_isMaskFilled.SetSize(_tileCount);
		_isTileFilled.SetSize(_tileCount);

		//_defaultLayerTiles.reserve(_tileCount);

		int k = 0;
		for (int i = 0; i < th; i++) {
			for (int j = 0; j < tw; j++) {
				bool maskEmpty = true;
				bool maskFilled = true;
				// TODO
				//bool tileFilled = true;

				//auto pixelOffset = &pixels[(i * Tiles::TileSet::DefaultTileSize * w) + (j * Tiles::TileSet::DefaultTileSize)];
				auto maskOffset = &_mask[k * DefaultTileSize * DefaultTileSize];
				for (int x = 0; x < DefaultTileSize * DefaultTileSize; x++) {
					bool masked = (maskOffset[x] > 0);
					maskEmpty &= !masked;
					maskFilled &= masked;

					//ColorRgba pxTex = texture[j * DefaultTileSize + x, i * DefaultTileSize + y];
					//masked = (pxTex.A > 20);
					//tileFilled &= masked;
				}

				//masks.Add(tileMask);

				int idx = (j + tw * i);
				if (maskEmpty) {
					_isMaskEmpty.SetBit(idx);
				}
				if (maskFilled) {
					_isMaskFilled.SetBit(idx);
				}
				if (/*tileFilled ||*/ !maskEmpty) {
					_isTileFilled.SetBit(idx);
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