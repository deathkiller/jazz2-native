#pragma once

#include "../ContentFileTypes.h"

namespace Jazz2::UI
{
	/**
		@brief On-disk layout of a packed font

		A font is a single self-contained file: the glyph metrics and the glyph atlas together, the atlas in the
		same QOI-based encoding every other image asset uses, all of it deflated. It holds one palette index per
		pixel rather than the expanded colours the old two-file (`.png` + `.png.font`) form carried, which cost
		four bytes per pixel on disk and again in memory.

		A font is entirely index based: every pixel either names a palette color or is index 0 and draws nothing.
		There is no per-pixel coverage - the shading of a glyph comes from the colors it names, not from blending
		it into what is behind it - so the packer resolves anything in between and says so.

		Glyphs are packed tightly: every one is measured to its actual inked area rather than sitting in a fixed
		cell, and each keeps a bearing so it still lands where it belongs on the line. A one pixel margin is left
		around each - so neighbours are two pixels apart - which is what stops bilinear sampling from bleeding one
		glyph into the next, the same margin the sprite sheets leave around each frame.

		The layout is:
		-   @ref Signature, @cpp ContentFileType::Font @ce, @ref CurrentVersion and a flags byte
		-   the size of the deflated block that follows, everything below being inside it
		-   the atlas size, the line height, the base spacing and the character counts
		-   a @ref Glyph for each ASCII character, then a codepoint and a @ref Glyph for each Unicode one
		-   the atlas itself, one palette index per pixel
	*/
	struct FontFormat
	{
		/** @brief Signature shared by the game's own file formats */
		static constexpr std::uint64_t Signature = 0x2095A59FF0BFBBEF;
		/** @brief Version of the layout described here */
		static constexpr std::uint8_t CurrentVersion = 2;
		/** @brief Margin left around every glyph, in pixels (so neighbours are two pixels apart) */
		static constexpr std::int32_t GlyphMargin = 1;
		/** @brief Codepoint of the glyph drawn in place of a character the font doesn't have */
		static constexpr std::uint32_t FallbackCodepoint = 0;

		/**
			@brief Metrics of a single glyph

			@ref X, @ref Y, @ref Width and @ref Height bound the inked pixels inside the atlas. @ref BearingX and
			@ref BearingY place that box relative to the pen position, and @ref Advance is how far the pen then
			moves - so a trimmed glyph draws exactly where a fixed-cell one used to.

			A glyph with no ink at all (a space) has a zero @ref Width and @ref Height and is never drawn, only
			advanced over.
		*/
		struct Glyph
		{
			std::uint16_t X, Y;
			std::uint8_t Width, Height;
			std::int8_t BearingX, BearingY;
			std::uint8_t Advance;
		};

		/** @brief Largest glyph box the record above can describe */
		static constexpr std::int32_t MaxGlyphSize = 255;
		/** @brief Largest bearing (in absolute value) the record above can describe */
		static constexpr std::int32_t MaxGlyphBearing = 127;
	};
}
