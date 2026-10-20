#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

namespace Jazz2::Editor
{
	class IEditorContext;

	/**
	 * @brief Makes the selected cells ground, their tiles chosen by their neighbors, as one step
	 *
	 * The tiles around that the levels use with their neighbors as they're then stay as they are, the others are
	 * chosen again too (see @ref SmartTileModel and @ref TerrainRules). The cells are chosen from the left and from the
	 * top, a cell not chosen yet is solid for its neighbors, but no tile they're chosen to go well with, and the tiles
	 * around are chosen with the selection as it's then. Returns `false` if nothing changed, or the selection has more
	 * than about a million tiles, it's all done at once.
	 */
	bool FillSelectionWithGround(IEditorContext& session);

	/**
	 * @brief Chooses better looking tiles for the selected ones, each with the same collision mask, as one step
	 *
	 * The collision stays exactly as it was: each tile can become another tile of the tile set with the same mask,
	 * flipped or not, the one that goes best with the tiles around it, as the levels of the tile set put tiles next
	 * to each other. A row is chosen as a whole, the best path through the tiles that can be in it, then each tile
	 * again with all of its neighbors known. Returns `false` if nothing changed, or the selection is too large, as
	 * for @ref FillSelectionWithGround().
	 */
	bool BeautifySelection(IEditorContext& session);
}

#endif
