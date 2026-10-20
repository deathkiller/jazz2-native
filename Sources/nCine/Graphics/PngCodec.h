#pragma once

#include "../../Main.h"

#include <memory>

#include <Containers/SmallVector.h>
#include <IO/Stream.h>

/**
	@brief Whether @ref nCine::PngCodec can write images too

	The game itself never writes a PNG image, only the level editor and the tools do, so the encoder is left out of
	the builds that don't need it --- the console ones above all. It's defined by @cpp WITH_EDITOR @ce, a tool that
	writes images (AssetPacker) defines it on its own.
*/
#if (defined(WITH_EDITOR) && !defined(WITH_PNG_ENCODER)) || defined(DOXYGEN_GENERATING_OUTPUT)
#	define WITH_PNG_ENCODER
#endif

namespace nCine
{
	/**
		@brief An image as @ref PngCodec::Decode() reads and @ref PngCodec::Encode() writes it

		Either palette indices, a byte per pixel, with the palette, or RGBA, four bytes per pixel. Colors of the palette
		are `R | G << 8 | B << 16 | A << 24`, as the palettes of the game have them; the entries the file doesn't list
		are zero, transparent black.
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
		Death::Containers::SmallVector<std::uint8_t, 0> Pixels;
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
		@brief An image as @ref PngCodec::DecodeRgba() reads it, always RGBA, four bytes per pixel
	*/
	struct PngRgbaImage
	{
		/** @brief Width in pixels */
		std::int32_t Width = 0;
		/** @brief Height in pixels */
		std::int32_t Height = 0;
		/** @brief RGBA pixels row by row, in this order of bytes on every machine */
		std::unique_ptr<std::uint8_t[]> Pixels;

		/** @brief Returns `true` if the image holds pixels */
		explicit operator bool() const {
			return (Pixels != nullptr);
		}
	};

	/** @brief Why @ref PngCodec couldn't read an image */
	enum class PngError : std::uint8_t
	{
		None,			/**< It could */
		NotPng,			/**< The data isn't a PNG image */
		EndsTooSoon,	/**< The data ends before the image does */
		Damaged,		/**< The image is damaged */
		TooLarge,		/**< The image has more pixels than allowed, see @ref PngDecodeOptions::MaxPixels */
		UnknownKind,	/**< The image is of a kind PNG doesn't have */
		Unsupported,	/**< The image uses a feature that isn't supported */
		NoPalette		/**< An indexed image has no palette */
	};

	/** @brief Default of @ref PngDecodeOptions::MaxPixels, 16384 × 16384, or 8192 × 8192 with 32-bit addresses */
	constexpr std::int64_t PngDefaultMaxPixels = (sizeof(void*) >= 8 ? (std::int64_t(1) << 28) : (std::int64_t(1) << 26));

	/** @brief How @ref PngCodec reads an image */
	struct PngDecodeOptions
	{
		/**
		 * @brief Whether the checksums of the chunks that are read are checked
		 *
		 * Off by default --- images of the game are read intact from its own content, some of them were written with
		 * the checksums left zero, and not computing them is faster. Images from anywhere else should be checked, a
		 * damaged critical chunk is refused then, a damaged ancillary one is ignored. An ancillary chunk that isn't
		 * used (anything but `tRNS`) is skipped without its checksum, whatever it holds changes nothing.
		 */
		bool VerifyCrc = false;
		/** @brief Most pixels an image may have, so a damaged size can't take all of the memory */
		std::int64_t MaxPixels = PngDefaultMaxPixels;
	};

	/**
		@brief Reads and writes PNG images

		Reads all of PNG --- grayscale, true color, indexed, with alpha or without, 1 to 16 bits per channel, with the
		transparency of a palette or of a color, and interlaced images. 16 bits are rounded to the nearest of 8.
		Writes an indexed image with its palette and the transparency of its entries, everything else as RGBA, 8 bits
		per channel; only with @ref WITH_PNG_ENCODER.

		Images can come from anywhere, so nothing is allocated before the data shows it's there: chunks are read as
		their data comes, an image with more pixels than allowed, or with less data than its size needs, is refused,
		and the image is inflated a row at a time straight into its pixels, with only the row before it kept. All of it
		is read byte by byte, the same on machines of either order of bytes.
	*/
	class PngCodec
	{
	public:
		PngCodec() = delete;

		/** @brief Reads an image, an indexed one as indices with its palette, everything else as RGBA, the image is empty if it fails */
		static PngError Decode(Death::IO::Stream& s, PngImage& image, const PngDecodeOptions& options = {});
		/**
		 * @brief Reads an image as RGBA whatever it holds, the image is empty if it fails
		 *
		 * An indexed image gets the colors of its palette, an entry the file doesn't list is opaque black.
		 */
		static PngError DecodeRgba(Death::IO::Stream& s, PngRgbaImage& image, const PngDecodeOptions& options = {});
		/** @brief Returns what an error means, in English, for logs */
		static const char* GetErrorText(PngError error);

#if defined(WITH_PNG_ENCODER) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief Writes an image, returns `false` if it couldn't, or any write to the stream failed */
		static bool Encode(Death::IO::Stream& s, const PngImage& image);
		/** @brief Writes RGBA pixels, four bytes per pixel row by row, returns `false` if it couldn't, or any write failed */
		static bool EncodeRgba(Death::IO::Stream& s, std::int32_t width, std::int32_t height, const std::uint8_t* pixels);
#endif
	};
}
