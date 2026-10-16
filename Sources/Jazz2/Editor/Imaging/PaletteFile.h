#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <Containers/String.h>
#include <Containers/StringView.h>

#include <cstdint>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::String;
	using Death::Containers::StringView;
#endif

	/**
		@brief Reads and writes palettes in the files other programs use, by their extension

		- `.pal` --- JASC (Paint Shop Pro and many others), text: `JASC-PAL`, `0100`, the number of colors, then a
		  line of `R G B` for each
		- `.gpl` --- GIMP, text: `GIMP Palette`, optional `Name:` and `Columns:` lines, `#` comments, then `R G B`
		  and an optional name for each color
		- `.act` --- Adobe Color Table, 256 × `R G B` bytes, optionally followed by a big-endian 16-bit number of
		  colors and the index of the transparent one
		- `.png` --- the palette of an indexed image; a true color image whose size divides into 16 × 16 cells gives
		  the color in the middle of each, a smaller one the colors of up to 256 pixels row by row. It's written
		  indexed, 16 × 16 cells of 8 × 8 pixels, each in its entry.

		Colors are `R | G << 8 | B << 16 | A << 24`, as the palettes of the game have them, read colors are opaque.
	*/
	class PaletteFile
	{
	public:
		PaletteFile() = delete;

		/** @brief Returns `true` if the extension of a path is of a palette file this can read and write */
		static bool IsSupported(StringView path);

		/**
		 * @brief Reads a palette, returns the number of colors it has, or 0 if it can't be read
		 *
		 * Colors it doesn't have are left as they are. On failure, @p error says why.
		 */
		static std::int32_t Read(StringView path, std::uint32_t (&palette)[256], String* error = nullptr);
		/** @brief Writes all 256 colors of a palette, returns `false` if it can't be written */
		static bool Write(StringView path, const std::uint32_t (&palette)[256], StringView name = {});
	};
}

#endif
