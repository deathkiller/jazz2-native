#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Document/LevelDocument.h"
#include "../../../nCine/Base/BitArray.h"
#include "../../../nCine/Primitives/Rect.h"

namespace Jazz2::Editor
{
	/**
		@brief Tiles the brush paints with

		A rectangle of tiles, each of which can be left out so the stamp only paints some of them. Painted along a
		stroke, it repeats from where the stroke started, so a stamp of a pattern makes a seamless pattern.
	*/
	struct TileStamp
	{
		/** @brief Width in tiles */
		std::int32_t Width = 0;
		/** @brief Height in tiles */
		std::int32_t Height = 0;
		/** @brief Tiles row by row */
		SmallVector<TileRef, 0> Tiles;
		/** @brief Whether each tile is painted at all */
		SmallVector<bool, 0> Present;
		/**
			@brief Whether each cell gets one of the tiles at random instead of the pattern

			A tile that's there more times comes more often, a tile left out leaves cells as they are. The choice is the
			same for the same position and @ref Seed, so painting over a cell again doesn't change it.
		*/
		bool Random = false;
		/** @brief Seed of the random choice, another one chooses other tiles */
		std::uint32_t Seed = 0;

		/** @brief Returns `true` if there's nothing to paint */
		bool IsEmpty() const {
			return (Width <= 0 || Height <= 0);
		}
		/** @brief Makes a stamp of a single tile */
		static TileStamp FromTile(const TileRef& tile);
		/** @brief Makes a stamp of a rectangle of a layer */
		static TileStamp FromLayer(const DocumentLayer& layer, const Recti& area);
		/** @brief Makes a stamp of a rectangle of tiles of a tile set, given its width in tiles */
		static TileStamp FromTileSet(std::uint8_t source, std::int32_t columns, const Recti& area);

		/** @brief Returns the tile at a position of the stamp */
		const TileRef& GetTile(std::int32_t x, std::int32_t y) const {
			return Tiles[std::size_t(y) * Width + x];
		}
		/** @brief Returns `true` if the tile at a position of the stamp is painted */
		bool IsPresent(std::int32_t x, std::int32_t y) const {
			return Present[std::size_t(y) * Width + x];
		}
		/** @brief Returns the tile of a pattern of the stamp repeated from an origin, for a position of a layer */
		bool GetPatternTile(std::int32_t x, std::int32_t y, Vector2i origin, TileRef& tile) const;

		/** @brief Flips the stamp horizontally, the tiles themselves too */
		void FlipX();
		/** @brief Flips the stamp vertically, the tiles themselves too */
		void FlipY();
	};

	/**
		@brief Selected tiles of a layer
	*/
	class TileSelection
	{
	public:
		/** @brief Returns `true` if nothing is selected */
		bool IsEmpty() const {
			return (_bounds.W <= 0 || _bounds.H <= 0);
		}
		/** @brief Returns the layer of the selection, or -1 */
		std::int32_t GetLayer() const {
			return _layer;
		}
		/** @brief Returns the bounds of the selected tiles */
		const Recti& GetBounds() const {
			return _bounds;
		}
		/** @brief Returns `true` if a tile is selected */
		bool Contains(std::int32_t x, std::int32_t y) const;

		/** @brief Clears the selection */
		void Clear();
		/** @brief Selects tiles of a layer, given its size, replacing or adding to what was selected */
		void Select(std::int32_t layer, Vector2i layerSize, const Recti& area, bool add);
		/** @brief Selects tiles given by a function, replacing or adding to what was selected */
		void Select(std::int32_t layer, Vector2i layerSize, const BitArray& mask, bool add);

	private:
		std::int32_t _layer = -1;
		Vector2i _layerSize;
		Recti _bounds;
		BitArray _mask;

		void UpdateBounds();
		void Reset(std::int32_t layer, Vector2i layerSize);
	};
}

#endif
