#pragma once

#include "../../Main.h"

#include "../../nCine/Base/BitArray.h"
#include "../../nCine/Graphics/Texture.h"

#include <memory>

#include <Containers/ArrayView.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2::Tiles
{
	/** @brief Represents tile set used by tile map, consists of texture and collision mask */
	class TileSet
	{
	public:
		/** @brief Size of a tile */
		static constexpr std::int32_t DefaultTileSize = 32;

		TileSet(StringView path, std::uint16_t tileCount, std::unique_ptr<Texture> textureDiffuse, std::unique_ptr<std::uint8_t[]> mask, std::uint32_t maskSize, std::unique_ptr<Color[]> captionTile);

		/** @brief Relative path to source file */
		String FilePath;
		/** @brief Main (diffuse) texture */
		std::unique_ptr<Texture> TextureDiffuse;
		/** @brief Total number of tiles */
		std::int32_t TileCount;
		/** @brief Number of tiles per row */
		std::int32_t TilesPerRow;

		/** @brief Returns mask for specified tile */
		std::uint8_t* GetTileMask(std::int32_t tileId) const
		{
			if (tileId >= TileCount) {
				return nullptr;
			}

			return &_mask[tileId * DefaultTileSize * DefaultTileSize];
		}

		/** @brief Returns `true` if the mask of a tile is completely empty */
		bool IsTileMaskEmpty(std::int32_t tileId) const
		{
			if (tileId >= TileCount) {
				return true;
			}

			return _isMaskEmpty[tileId];
		}

		/** @brief Returns `true` if the mask of a tile is completely filled (non-empty) */
		bool IsTileMaskFilled(std::int32_t tileId) const
		{
			if (tileId >= TileCount) {
				return false;
			}

			return _isMaskFilled[tileId];
		}

		/** @brief Returns `true` if the texture of a tile is completely opaque (non-transparent) */
		bool IsTileFilled(std::int32_t tileId) const
		{
			if (tileId >= TileCount) {
				return false;
			}

			return _isTileFilled[tileId];
		}

		/** @brief Returns a caption tile */
		StaticArrayView<DefaultTileSize * DefaultTileSize, Color> GetCaptionTile() const
		{
			return staticArrayView<DefaultTileSize * DefaultTileSize>(_captionTile.get());
		}

		/** @brief Overrides the diffuse texture of the specified tile */
		bool OverrideTileDiffuse(std::int32_t tileId, StaticArrayView<(DefaultTileSize + 2) * (DefaultTileSize + 2), std::uint32_t> tileDiffuse);
		/** @brief Overrides the collision mask of the specified tile */
		bool OverrideTileMask(std::int32_t tileId, StaticArrayView<DefaultTileSize * DefaultTileSize, std::uint8_t> tileMask);

	private:
		std::unique_ptr<uint8_t[]> _mask;
		std::unique_ptr<Color[]> _captionTile;
		BitArray _isMaskEmpty;
		BitArray _isMaskFilled;
		BitArray _isTileFilled;
	};
}