#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "TerrainRules.h"
#include "../Document/LevelDocument.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>

namespace Jazz2::Formats
{
	struct TileSetData;
}

namespace Jazz2::Editor
{
	using Death::Containers::SmallVector;
	using Death::Containers::String;

	/**
		@brief Shapes of the collision masks of the tiles of a tile set

		A tile whose mask is solid below a straight line across all of it, and empty above it, is a piece of ground ---
		flat, or a slope of any steepness the line has. Its surface is known by how deep the line is at the left and
		the right edge of the tile. Each such tile is also known flipped horizontally, as a slope the other way.
	*/
	class TileShapes
	{
	public:
		/** @brief Tile of ground with a straight surface */
		struct Surface
		{
			/** @brief The tile, possibly flipped */
			TileRef Tile;
			/** @brief Depth of the surface at the left edge, in pixels from the top of the tile */
			float Left;
			/** @brief Depth of the surface at the right edge */
			float Right;
			/** @brief How far the mask is from the straight surface, on average in pixels */
			float Error;
		};

		/**
		 * @brief Analyzes the masks of the default tile set of a level, unless they're analyzed already
		 *
		 * The slope tags of @p rules go before what the masks tell, a tile left out isn't ground, a tagged one has
		 * the surface it's tagged with.
		 */
		void Build(StringView tileSet, const Formats::TileSetData& data, const TerrainRules* rules = nullptr);
		/** @brief Returns the tile set analyzed */
		StringView GetTileSet() const {
			return _tileSet;
		}
		/** @brief Returns all tiles of ground with a straight surface */
		const SmallVector<Surface, 0>& GetSurfaces() const {
			return _surfaces;
		}
		/**
		 * @brief Returns all tiles of ceiling, solid above a straight line, the depths are of the line from the top
		 *
		 * Pieces of ground flipped upside down are among them too, a little worse than tiles made for it.
		 */
		const SmallVector<Surface, 0>& GetCeilings() const {
			return _ceilings;
		}

	private:
		String _tileSet;
		SmallVector<Surface, 0> _surfaces;
		SmallVector<Surface, 0> _ceilings;
		std::uint32_t _rulesRevision = ~0u;
	};
}

#endif
