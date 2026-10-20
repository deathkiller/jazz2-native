#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"
#include "../../../nCine/Graphics/PngCodec.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>
#include <IO/Stream.h>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::SmallVector;
	using Death::Containers::String;
	using Death::Containers::StringView;
	using Death::IO::Stream;
	using nCine::PngImage;
#endif

	/**
		@brief Reads and writes PNG images for the editor

		Images of the editor come from anywhere, so they're read by @ref nCine::PngCodec with the checksums of the
		chunks checked, and why one can't be read is told in the language of the interface. An image of more than
		8192 × 8192 pixels (4096 × 4096 with 32-bit addresses) is refused, that holds the largest tile set.
	*/
	struct PngCodec
	{
		/** @brief Most pixels of an image the editor reads */
		static constexpr std::int64_t MaxPixels = (sizeof(void*) >= 8 ? (std::int64_t(1) << 26) : (std::int64_t(1) << 24));

		/** @brief Reads an image, returns `false` and why, translated, if it isn't a PNG image it can read, the image is empty then */
		static bool Decode(Stream& s, PngImage& image, String* error = nullptr);
		/** @brief Reads an image from a file, returns `false` and why, translated, if it can't be read */
		static bool Load(StringView path, PngImage& image, String* error = nullptr);
		/**
		 * @brief Reads an image from a file and the collision masks next to it, `<name>.mask.png`, as the editor exports them
		 *
		 * Returns `false` and why only if the image itself can't be read. @p hasMasks tells whether the masks were there
		 * and could be read, they're optional.
		 */
		static bool LoadWithMasks(StringView path, PngImage& image, PngImage& masks, bool& hasMasks, String* error = nullptr);
		/** @brief Returns the file of the collision masks of an image, `<name>.mask.png` next to `<name>.png` */
		static String GetMasksPath(StringView path);
		/** @brief Writes an image, returns `false` if it couldn't, or any write to the stream failed */
		static bool Encode(Stream& s, const PngImage& image);
		/** @brief Returns what an error of @ref nCine::PngCodec means, translated */
		static String GetErrorMessage(nCine::PngError error);
	};
}

#endif
