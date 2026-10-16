#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "PaletteMatch.h"

#include <Containers/ArrayView.h>

namespace Jazz2::Editor
{
	/**
		@brief Picks colors for the free entries of a palette, so the colors of an image look as close as they can

		@p colors are RGB colors as `R | G << 8 | B << 16`, alpha is ignored, each counts as many times as it's there.
		Entries for which @p locked is `true` keep their colors, and colors of the image are mapped to them too. Entry
		0 is transparent, it's always locked. Colors are compared in @ref OkLab --- the boxes of a median cut give the
		first colors, which then move to the middles of the colors closest to them for a few rounds (k-means). Returns
		the number of entries that were given a color.
	*/
	std::int32_t BuildPalette(Death::Containers::ArrayView<const std::uint32_t> colors, std::uint32_t (&palette)[256], const bool (&locked)[256]);

	/**
		@brief Finds the entries of a palette closest to colors, and remembers them

		With dithering, a 4 × 4 ordered pattern shifts the colors a little before they're looked up, which mixes the
		two closest entries where a color lies between them. The pattern is aligned to the pixels the colors are at, so
		tiles next to each other continue it without a seam.
	*/
	class PaletteMapper
	{
	public:
		/** @brief Creates a mapper to the entries 1 to 255 of a palette */
		PaletteMapper(const std::uint32_t* palette, bool dither);

		/** @brief Returns the entry for a color `R | G << 8 | B << 16` at a pixel */
		std::uint8_t Map(std::uint32_t color, std::int32_t x, std::int32_t y);

	private:
		OkLab _colors[256];
		std::uint32_t _palette[256];
		bool _dither;
		// Colors looked up already, by color and the cell of the pattern
		struct CacheEntry
		{
			std::uint32_t Key;
			std::uint8_t Index;
			bool Used;
		};
		static constexpr std::int32_t CacheSize = 1 << 14;
		CacheEntry _cache[CacheSize];

		std::uint8_t FindNearest(const OkLab& color) const;
	};
}

#endif
