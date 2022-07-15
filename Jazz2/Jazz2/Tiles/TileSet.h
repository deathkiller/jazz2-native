#pragma once

#include "../ILevelHandler.h"
#include "../../nCine/Base/BitArray.h"

#include <SmallVector.h>

namespace Jazz2::Tiles
{
	class TileSet
	{
		friend class TileMap;

	public:
		static constexpr int DefaultTileSize = 32;

		TileSet(std::unique_ptr<Texture> textureDiffuse, std::unique_ptr<uint8_t[]> mask);

		uint8_t* GetTileMask(int tileId) const
		{
			if (tileId >= _tileCount) {
				return nullptr;
			}

			return &_mask[tileId * DefaultTileSize * DefaultTileSize];
		}

		bool IsTileMaskEmpty(int tileId) const
		{
			if (tileId >= _tileCount) {
				return true;
			}

			return _isMaskEmpty[tileId];
		}

		bool IsTileMaskFilled(int tileId) const
		{
			if (tileId >= _tileCount) {
				return false;
			}

			return _isMaskFilled[tileId];
		}

		bool IsTileFilled(int tileId) const
		{
			if (tileId >= _tileCount) {
				return false;
			}

			return _isTileFilled[tileId];
		}

	private:
		std::unique_ptr<Texture> _textureDiffuse;
		std::unique_ptr<uint8_t[]> _mask;

		int _tileCount;
		int _tilesPerRow;
		BitArray _isMaskEmpty;
		BitArray _isMaskFilled;
		BitArray _isTileFilled;
	};
}