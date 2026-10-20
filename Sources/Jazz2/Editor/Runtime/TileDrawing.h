#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Document/LevelDocument.h"
#include "../UI/UiPainter.h"

namespace Jazz2::Tiles
{
	class TileMap;
}

namespace Jazz2::Editor
{
	/**
	 * @brief Draws a tile of a level by where it comes from
	 *
	 * A tile of a tile set is drawn as itself even if the level can't address it yet (see
	 * @ref LevelDocument::IsTileAddressable()), its tile ID would be read as an animated tile otherwise. Only an
	 * animated tile and a raw tile ID are drawn as the level file would store them, see @ref UiPainter::Tile(). The
	 * flags are given apart from the tile, as not every place draws its flips. Returns `false` if the tile doesn't
	 * exist.
	 */
	bool DrawTile(UiPainter& painter, Tiles::TileMap& tileMap, const LevelDocument& document, const TileRef& tile, std::uint8_t flags,
		const Rectf& rect, float alpha = 1.0f, UiLayer layer = UiLayer::Content);
}

#endif
