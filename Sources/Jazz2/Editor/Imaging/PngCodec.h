#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <IO/Stream.h>

namespace Jazz2::Editor
{
	using Death::Containers::SmallVector;
	using Death::Containers::String;
	using Death::IO::Stream;

	/**
		@brief An image as @ref PngCodec reads and writes it

		Either palette indices, a byte per pixel, with the palette, or RGBA, four bytes per pixel. Colors of the palette
		are `R | G << 8 | B << 16 | A << 24`, as the palettes of the game have them.
	*/
	struct PngImage
	{
		/** @brief Width in pixels */
		std::int32_t Width = 0;
		/** @brief Height in pixels */
		std::int32_t Height = 0;
		/** @brief Whether the pixels are palette indices instead of RGBA */
		bool Indexed = false;
		/** @brief Pixels row by row */
		SmallVector<std::uint8_t, 0> Pixels;
		/** @brief Colors of the palette, only of an indexed image */
		std::uint32_t Palette[256] = {};
		/** @brief Number of colors of the palette */
		std::int32_t PaletteSize = 0;

		/** @brief Makes an empty image of a size, all of its pixels zero */
		void Create(std::int32_t width, std::int32_t height, bool indexed);
		/** @brief Returns the color of a pixel as RGBA, an indexed one from the palette */
		std::uint32_t GetColor(std::int32_t x, std::int32_t y) const;
	};

	/**
		@brief Reads and writes PNG images

		Reads all of PNG --- grayscale, true color, indexed, with alpha or without, 1 to 16 bits per channel, with
		the transparency of a palette or of a color, and interlaced images. 16 bits are rounded to the nearest of 8. An indexed image is
		read as indices, everything else as RGBA. Writes an indexed image with its palette and the transparency of its
		entries, everything else as RGBA, 8 bits per channel.
	*/
	struct PngCodec
	{
		/** @brief Reads an image, returns `false` and why if it isn't a PNG image it can read */
		static bool Decode(Stream& s, PngImage& image, String* error = nullptr);
		/** @brief Writes an image, returns `false` if it couldn't */
		static bool Encode(Stream& s, const PngImage& image);
	};
}

#endif
