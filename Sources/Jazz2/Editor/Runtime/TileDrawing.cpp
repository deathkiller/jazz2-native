#include "TileDrawing.h"

#if defined(WITH_EDITOR)

#include "../../Tiles/TileMap.h"

namespace Jazz2::Editor
{
	bool DrawTile(UiPainter& painter, Tiles::TileMap& tileMap, const LevelDocument& document, const TileRef& tile, std::uint8_t flags,
		const Rectf& rect, float alpha, UiLayer layer)
	{
		std::int32_t tileId = document.ToTileId(tile);
		if (tile.Source == TileRef::Animated || tile.Source == TileRef::Raw) {
			return painter.Tile(tileMap, tileId, flags, rect, alpha, layer);
		}
		// A tile of a tile set only gets 0 as its ID if its tile set isn't known, which is the empty tile
		if (tileId <= 0 && !tile.IsEmpty()) {
			return false;
		}
		Texture* texture;
		Vector4f texCoords;
		bool indexed;
		if (!tileMap.GetStaticTileQuad(tileId, Tiles::LayerTileFlags(flags & 0x03), texture, texCoords, indexed)) {
			return false;
		}
		painter.Image(*texture, rect, texCoords, Colorf(1.0f, 1.0f, 1.0f, alpha), indexed ? 0 : -1, layer);
		return true;
	}
}

#endif
