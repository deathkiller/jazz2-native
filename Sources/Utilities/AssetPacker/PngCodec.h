#pragma once

#include "../../Main.h"

#include <memory>

#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::AssetPacker
{
	/**
		@brief A decoded image, always four bytes per pixel

		The fonts keep a palette index in the red channel and coverage in the alpha channel, which is what the
		rest of the tool works with; the two other channels are only carried so an ordinary image editor shows
		something recognisable once @ref FontPacker applies a palette.
	*/
	struct Image
	{
		std::int32_t Width = 0;
		std::int32_t Height = 0;
		/** @brief RGBA, row by row */
		std::unique_ptr<std::uint8_t[]> Pixels;

		explicit operator bool() const {
			return (Pixels != nullptr);
		}
	};

	/**
		@brief Minimal PNG reader and writer

		The game has a PNG decoder of its own, but it lives in the renderer, which this tool deliberately doesn't
		link - so it carries its own. It reads the 8-bit non-interlaced images an editor produces (greyscale,
		palette, RGB and RGBA, with or without alpha) and writes RGBA, which is enough for both ends of the font
		round trip.
	*/
	class PngCodec
	{
	public:
		/** @brief Reads an image, expanding whatever it finds to RGBA */
		static bool Read(StringView path, Image& image);
		/** @brief Writes an RGBA image */
		static bool Write(StringView path, const Image& image);
	};
}
