#pragma once

#include "../ILevelHandler.h"
#include "../../nCine/Base/BitArray.h"

namespace Jazz2::Tiles
{
	class TileSet
	{
	public:
		static constexpr std::int32_t DefaultTileSize = 32;

		TileSet(std::uint16_t tileCount, std::unique_ptr<Texture> textureDiffuse, std::unique_ptr<std::uint8_t[]> mask, std::uint32_t maskSize, std::unique_ptr<Color[]> captionTile);

		std::unique_ptr<Texture> TextureDiffuse;
		std::int32_t TileCount;
		std::int32_t TilesPerRow;

		std::uint8_t* GetTileMask(std::int32_t tileId) const
		{
			if (tileId >= TileCount) {
				return nullptr;
			}

			return &_mask[tileId * DefaultTileSize * DefaultTileSize];
		}

		bool IsTileMaskEmpty(std::int32_t tileId) const
		{
			if (tileId >= TileCount) {
				return true;
			}

			return _isMaskEmpty[tileId];
		}

		bool IsTileMaskFilled(std::int32_t tileId) const
		{
			if (tileId >= TileCount) {
				return false;
			}

			return _isMaskFilled[tileId];
		}

		bool IsTileFilled(std::int32_t tileId) const
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