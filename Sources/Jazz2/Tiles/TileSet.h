#pragma once

#include "../../Main.h"

#include "../../nCine/Base/BitArray.h"
#include "../../nCine/Graphics/Texture.h"

#include <memory>

#include <Containers/ArrayView.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2::Tiles
{
	/**
		@brief Represents tile set used by tile map, consists of texture and collision mask
		
		Holds the source texture and the per-tile collision masks for a single tile set, providing lookups for a
		tile's mask and texture coordinates. A tile map references one or more of these to render and collide its
		layers; textures may be true-color or indexed for palette recoloring.
	*/
	class TileSet
	{
	public:
		/** @brief Size of a tile */
		static constexpr std::int32_t DefaultTileSize = 32;
		/** @brief Byte size of one tile's packed collision mask (1 bit per pixel, LSB-first, row-major - the cache file format) */
		static constexpr std::int32_t MaskBytesPerTile = DefaultTileSize * DefaultTileSize / 8;

		/**
		 * @brief Creates a new instance
		 *
		 * @param path               Relative path to the source file
		 * @param tileCount          Total number of tiles
		 * @param textureDiffuse     Main (diffuse) texture, or several row-band chunks when the device
		 *                           texture-size limit forced a split (see `ContentResolver::BuildTilesetDiffuse`)
		 * @param mask               Packed collision mask of all tiles (@ref MaskBytesPerTile bytes each)
		 * @param maskSize           Size of the packed collision mask in bytes
		 * @param captionTile        Pixels of the caption tile
		 * @param tileDiffuseOpaque  Optional table marking fully opaque tiles
		 */
		TileSet(StringView path, std::uint16_t tileCount, SmallVector<std::unique_ptr<Texture>, 1>&& textureDiffuse, std::unique_ptr<std::uint8_t[]> mask, std::uint32_t maskSize, std::unique_ptr<Color[]> captionTile, const std::uint8_t* tileDiffuseOpaque = nullptr);

		/** @brief Relative path to source file */
		String FilePath;
		/** @brief Main (diffuse) texture(s): a single texture normally, consecutive row-band chunks when the device texture-size limit forced a split */
		SmallVector<std::unique_ptr<Texture>, 1> TextureDiffuse;
		/** @brief Total number of tiles */
		std::int32_t TileCount;
		/** @brief Number of tiles per row */
		std::int32_t TilesPerRow;
		/** @brief Number of tiles covered by each diffuse texture chunk (0 when untextured/headless) */
		std::int32_t TilesPerTexture;
		/**
		 * @brief Whether @ref TextureDiffuse stores raw palette indices instead of baked colors
		 *
		 * Indexed tiles (red channel holds the index) are recolored at draw time through the palette shaders. `false`
		 * for tilesets containing 32-bit true-color tiles.
		 */
		bool IsIndexed = false;
		/**
		 * @brief Maps a tile ID to the slot it occupies in the diffuse texture, or `nullptr` when they are the same
		 *
		 * A tileset describes every tile its author drew, but a level references only a part of them - under half,
		 * on the levels measured. Where memory is scarce the atlas holds only the referenced tiles, packed
		 * together, and this table says where each ID ended up. Slot 0 is left blank and is what every pruned or
		 * out-of-range ID maps to, so a tile that appears later (a script placing one, say) draws as empty rather
		 * than sampling whatever happens to sit at its old position.
		 */
		std::unique_ptr<std::uint16_t[]> AtlasSlot;

		/**
		 * @brief Returns the atlas slot a tile ID occupies, which is the ID itself unless the atlas was packed
		 *
		 * Anything that treats an ID as a position in the diffuse texture - the UV maths, and the chunk index
		 * when the atlas was split - has to go through this first. One array load; the per-tile collision data
		 * stays indexed by the original ID.
		 */
		std::int32_t MapToAtlasSlot(std::int32_t tileId) const
		{
			if (AtlasSlot == nullptr) {
				return tileId;
			}
			return (tileId >= 0 && tileId < TileCount ? (std::int32_t)AtlasSlot[tileId] : 0);
		}

		/**
		 * @brief Resolves the diffuse texture chunk containing @p tileId, rebasing the ID into the chunk's
		 *        local space (mirrors `TileMap::ResolveTileSet`)
		 *
		 * The returned texture's tile grid starts at the rebased @p tileId 0, so the usual
		 * `(tileId % TilesPerRow, tileId / TilesPerRow)` UV math applies against ITS size. Returns `nullptr`
		 * when untextured (headless) or out of range.
		 */
		Texture* ResolveTextureDiffuse(std::int32_t& tileId) const
		{
			if (TextureDiffuse.empty()) {
				return nullptr;
			}
			tileId = MapToAtlasSlot(tileId);
			if (tileId >= TilesPerTexture && TilesPerTexture > 0) {
				std::int32_t chunk = tileId / TilesPerTexture;
				if (chunk >= std::int32_t(TextureDiffuse.size())) {
					return nullptr;
				}
				tileId -= chunk * TilesPerTexture;
				return TextureDiffuse[chunk].get();
			}
			return TextureDiffuse[0].get();
		}

		/** @brief Returns the number of diffuse texture chunks (1 unless the device texture-size limit forced a split) */
		std::int32_t GetTextureCount() const
		{
			return std::int32_t(TextureDiffuse.size());
		}

		/** @brief Returns the packed mask (@ref MaskBytesPerTile bytes, 1 bit per pixel) for the specified tile */
		const std::uint8_t* GetTileMask(std::int32_t tileId) const
		{
			if (tileId >= TileCount) {
				return nullptr;
			}

			return &_mask[tileId * MaskBytesPerTile];
		}

		/** @brief Returns one row of a packed tile mask as a 32-bit word (bit `x` set = column `x` solid) */
		static std::uint32_t GetTileMaskRow(const std::uint8_t* packedMask, std::int32_t y)
		{
			const std::uint8_t* row = packedMask + y * (DefaultTileSize / 8);
			return std::uint32_t(row[0]) | (std::uint32_t(row[1]) << 8) | (std::uint32_t(row[2]) << 16) | (std::uint32_t(row[3]) << 24);
		}

		/** @brief Returns whether pixel `(x, y)` of a packed tile mask is solid */
		static bool IsTileMaskBitSet(const std::uint8_t* packedMask, std::int32_t x, std::int32_t y)
		{
			// Bit index is y * 32 + x with LSB-first bytes, so the bit-within-byte is just x & 7
			return ((packedMask[(y * DefaultTileSize + x) >> 3] >> (x & 7)) & 1) != 0;
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

		/**
		 * @brief Returns `true` if every column of the tile's mask is vertically contiguous (no gaps)
		 *
		 * Contiguity allows replacing the per-pixel collision scan with an exact per-column span test.
		 */
		bool IsColumnContiguous(std::int32_t tileId) const
		{
			if (tileId >= TileCount) {
				return false;
			}

			return _isColumnContiguous[tileId];
		}

		/**
		 * @brief Returns per-column solid spans for a tile
		 *
		 * 2 bytes per column (first solid row, last solid row); a first row of `0xFF` marks an empty column. Only
		 * meaningful when @ref IsColumnContiguous() returns `true`.
		 */
		const std::uint8_t* GetColumnSpans(std::int32_t tileId) const
		{
			return &_columnSpans[tileId * DefaultTileSize * 2];
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
		std::unique_ptr<std::uint8_t[]> _columnSpans;
		std::unique_ptr<Color[]> _captionTile;
		BitArray _isMaskEmpty;
		BitArray _isMaskFilled;
		BitArray _isTileFilled;
		BitArray _isColumnContiguous;
	};
}