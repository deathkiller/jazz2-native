#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Formats/TileSetFile.h"
#include "../../../nCine/Primitives/Colorf.h"

namespace Jazz2::Editor
{
	/**
		@brief Finds the color a tile set is known by --- a vivid color that many of its pixels have

		The pixels are sorted by their hue, each weighted by how saturated and bright it is, so grays and dark shades
		don't count. The color of the hue with the most weight is returned light and only a little saturated.
		The 8-bit tiles take their colors from @p palette, as `R | G << 8 | B << 16`. A tile counts as much as its
		entry of @p tileWeights says, a tile without one doesn't count, all tiles count the same if it's empty.
		Returns `false` if no pixel that counts has a color like that.
	*/
	bool FindAccentColor(const Formats::TileSetData& tileSet, const std::uint32_t* palette, Death::Containers::ArrayView<const float> tileWeights,
		nCine::Colorf& color);
}

#endif
