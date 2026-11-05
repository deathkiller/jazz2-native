#pragma once

#if defined(WITH_UI_WINDOWS) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"

#include <Containers/StringView.h>

namespace Jazz2::UI::Windows
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::StringView;
#endif

	/**
		@brief Vector art described as text, drawn into pixels at any size

		The art is drawn on a square of @p designSize units, usually 16 as an icon, scaled to the pixels it's drawn
		into, with the y axis going down. It's a list of statements separated by `;`, each of them a paint and the
		shapes painted with it after `:`:

		-   `fill <color> [opacity]: <shapes>` fills the shapes, where they overlap or a shape goes around another one
			the same way, it's filled, a shape going around the other way cuts a hole.
		-   `stroke <color> <width> [opacity] [round|butt|square|miter|bevel|round-cap|round-join]: <shapes>` draws
			their outlines. `round` makes both the ends and the corners round, the default is `butt` ends and `miter`
			corners.
		-   `clear: <shapes>` takes away what is painted under the shapes so far, to cut a hole into it.
		-   `outline <width> [color]` gives everything painted after it a dark outline of the width all around, `0`
			ends it. All the outlines are drawn first, under all the paints, so they outline the whole of the art as
			the dark edge of the sprites of the game does, not every part of it.

		A color is a letter of @ref Icons::GetPixelColor() but the digits --- `#` is drawn in the color the art is
		drawn with --- or `#RRGGBB` and `#RRGGBBAA`. The shapes are paths of SVG, `M`, `L`, `H`, `V`, `C`, `S`, `Q`,
		`T`, `A` and `Z` in both cases, and `circle <cx> <cy> <r>`, `ellipse <cx> <cy> <rx> <ry>` and
		`rect <x> <y> <width> <height> [radius]`, all in units. For example, a tick:

		@code{.unparsed}
		stroke # 2 round: M3.5 8.5 L6.5 11.5 L12.5 4.5
		@endcode
	*/
	class VectorArt
	{
	public:
		/** @brief Returns the color of a letter as R, G, B and A bytes from the lowest, 0 if it's not a color */
		using ColorFunc = std::uint32_t(*)(char letter);

		VectorArt() = delete;

		/**
		 * @brief Draws vector art into a square of RGBA pixels, over what is there already
		 *
		 * @p size is the side of the square in pixels, @p stride the bytes from a row to the next one. Pixels outside
		 * of the square aren't touched. Returns `false` and draws nothing if the text can't be read, the place it
		 * couldn't be read at is logged.
		 */
		static bool Draw(StringView source, float designSize, std::int32_t size, std::uint8_t* pixels, std::int32_t stride, ColorFunc colors);

		/** @brief Returns `true` if the text can be read */
		static bool IsValid(StringView source, ColorFunc colors);
	};
}

#endif
