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
		/**
		 * @brief How often each tile is chosen when painting at random, the same for all if empty
		 *
		 * One for each of @ref Tiles, a tile with twice the weight comes twice as often, zero never.
		 */
		SmallVector<float, 0> Weights;

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
		/**
		 * @brief Turns the stamp by a quarter, clockwise or counter-clockwise
		 *
		 * A tile can only be flipped, not turned, so the tiles go where they would be turned, each keeps how it looks.
		 * Two quarters are the same as flipping both ways, which turns the tiles too.
		 */
		void Rotate(bool clockwise);
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
		/** @brief Selects the tiles of a layer that aren't selected, and only them */
		void Invert(std::int32_t layer, Vector2i layerSize);
		/** @brief Adds the tiles around the selected ones, or with @p shrink takes away the selected tiles at its edge */
		void Grow(bool shrink);
		/** @brief Returns the selected tiles as one bit for each tile of the layer, row by row */
		const BitArray& GetMask() const {
			return _mask;
		}
		/** @brief Returns the size of the layer of the selection */
		Vector2i GetLayerSize() const {
			return _layerSize;
		}
		/**
		 * @brief Returns a number that changes whenever the selection does
		 *
		 * The numbers come from one counter for all selections, so two selections with the same number are the same,
		 * what's drawn of a selection can be kept until its number changes.
		 */
		std::uint32_t GetRevision() const {
			return _revision;
		}

	private:
		std::int32_t _layer = -1;
		Vector2i _layerSize;
		Recti _bounds;
		BitArray _mask;
		std::uint32_t _revision = 0;

		// Gives the selection a number no selection had before, see GetRevision()
		void Touch();

		// Finds the bounds of the selected tiles in an area, nothing outside of it is selected
		void UpdateBounds(const Recti& area);
		// Grows the bounds by the bounds of tiles just selected
		void IncludeInBounds(const Recti& area);
		void Reset(std::int32_t layer, Vector2i layerSize);
	};
}

#endif
