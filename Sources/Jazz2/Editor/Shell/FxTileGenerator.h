#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <cstdint>

namespace Jazz2::Formats
{
	struct TileSetData;
}

namespace Jazz2::Editor
{
	class IEditorContext;

	/** @brief Effect the tiles of a blur behind layer are made with, see @ref GenerateFxTiles() */
	enum class FxTileKind : std::uint8_t
	{
		/** A clear pane that bends what's behind it a little, as a lens */
		ClearGlass,
		/** A wall of glass blocks, each a little lens that softens what's behind it */
		GlassBlocks,
		/** Frosted glass, what's behind it is blurred */
		FrostedGlass,
		/** Water seen from the side, it reflects near its top and blurs deeper down, in waves */
		Water,
		/** Rising heat, it shimmers what's behind it */
		HeatHaze,
		/** A round lens that magnifies what's behind it */
		Magnifier,
		/** A puddle on the ground, it reflects what's above it, a little rippled */
		Puddle,

		Count
	};

	/**
		@brief Makes the tiles of an effect for a blur behind layer, as true color tiles without collision

		A rectangle of @p widthTiles × @p heightTiles tiles, row by row. Their red and green are the displacement
		(128 is none, a step is a quarter of a pixel), blue the reflection and alpha the blur, 0 for no effect at all
		(see @ref Tiles::LayerRendererType::BlurBehind). @p strength from 0 to 1 scales the effect.
	*/
	void GenerateFxTiles(FxTileKind kind, std::int32_t widthTiles, std::int32_t heightTiles, float strength, Formats::TileSetData& tiles);

	/**
		@brief Asks for an effect and a size, adds the tiles of it to the tile set shown in the tile picker and makes
		them the brush, to paint them on a layer drawn as blur behind
	*/
	void ShowFxTileGeneratorDialog(IEditorContext& session);
}

#endif
