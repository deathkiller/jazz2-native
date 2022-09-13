#pragma once

#include "../ILevelHandler.h"
#include "../../nCine/Base/BitArray.h"

namespace Jazz2::Tiles
{
	class TileSet
	{
	public:
		static constexpr int DefaultTileSize = 32;

		TileSet(std::unique_ptr<Texture> textureDiffuse, std::unique_ptr<uint8_t[]> mask, std::unique_ptr<Color[]> captionTile);

		std::unique_ptr<Texture> TextureDiffuse;
		int TileCount;
		int TilesPerRow;

		uint8_t* GetTileMask(int tileId) const
		{
			if (tileId >= TileCount) {
				return nullptr;
			}

			return &_mask[tileId * DefaultTileSize * DefaultTileSize];
		}

		bool IsTileMaskEmpty(int tileId) const
		{
			if (tileId >= TileCount) {
				return true;
			}

			return _isMaskEmpty[tileId];
		}

		bool IsTileMaskFilled(int tileId) const
		{
			if (tileId >= TileCount) {
				return false;
			}

			return _isMaskFilled[tileId];
		}

		bool IsTileFilled(int tileId) const
		{
			if (tileId >= TileCount) {
				return false;
			}

			return _isTileFilled[tileId];
		}

		Color* GetCaptionTile() const
		{
			return _captionTile.get();
		}

	private:
		std::unique_ptr<uint8_t[]> _mask;
		std::unique_ptr<Color[]> _captionTile;
		BitArray _isMaskEmpty;
		BitArray _isMaskFilled;
		BitArray _isTileFilled;
	};
}