#pragma once

#include "ContentFileTypes.h"

namespace Jazz2
{
	/**
		@brief Layout of the game's own cinematic format

		The original `.j2v` videos store four interleaved zlib streams, and inflating them costs more than a
		frame's entire budget on the weakest platforms (measured at 55-115 ms per frame on the Dreamcast,
		against 42 ms available). This format keeps the same idea - 8-bit indexed frames encoded as changes
		against the previous one - but replaces the entropy coding with a byte-oriented codec whose decoder is
		nothing but @cb{.cpp} memcpy @ce and @cb{.cpp} memset @ce over a reused frame buffer.

		A file starts with the same signature and type/version fields every other file the game writes uses,
		followed by a fixed header, an extension area that older players skip, and then one payload per frame.
		Files are identified by their signature, so the player accepts both this and the original format.

		@par File layout
		- @cb{.cpp} u64 @ce signature, @cb{.cpp} u8 @ce file type, @cb{.cpp} u16 @ce version
		- @cb{.cpp} u16 @ce width, @cb{.cpp} u16 @ce height, @cb{.cpp} u16 @ce frame delay in milliseconds
		- @cb{.cpp} u32 @ce frame count
		- @cb{.cpp} u8 @ce pixel format, @cb{.cpp} u8 @ce codec, @cb{.cpp} u16 @ce size of the extension area
		- extension area (skipped when not understood), then per frame: @cb{.cpp} u32 @ce size + payload

		@par Frame payload
		A flag byte (bit 0 --- a 256 entry BGRA palette follows), then commands until @ref CommandEndOfFrame.
		Skipped spans are left as they are, which is what makes an unchanged region free: the frame buffer
		already holds the previous frame.
	*/
	struct VideoFormat
	{
		/** @brief Signature shared by all files the game writes */
		static constexpr std::uint64_t Signature = 0x2095A59FF0BFBBEF;
		/** @brief Version this build writes, and the highest it can read */
		static constexpr std::uint16_t CurrentVersion = 1;

		/** @brief 8 bits per pixel through a 256 entry palette */
		static constexpr std::uint8_t PixelFormatIndexed8 = 0;
		/** @brief Per-frame changes as skip/literal/run commands (see @ref VideoFormat) */
		static constexpr std::uint8_t CodecDeltaRle = 0;

		/** @brief Set in a frame's flag byte when a palette precedes its commands */
		static constexpr std::uint8_t FrameFlagPalette = 0x01;

		/** @brief `0x00`-`0x7F`: copy this many + 1 bytes from the payload */
		static constexpr std::uint8_t CommandLiteralMax = 0x7F;
		/** @brief `0x80`-`0xBF`: repeat the following byte this many - `0x80` + 3 times */
		static constexpr std::uint8_t CommandRunBase = 0x80;
		static constexpr std::uint8_t CommandRunMax = 0xBF;
		static constexpr std::uint8_t CommandRunMinLength = 3;
		/** @brief `0xC0`-`0xDF`: leave this many - `0xC0` + 1 bytes of the previous frame in place */
		static constexpr std::uint8_t CommandSkipBase = 0xC0;
		static constexpr std::uint8_t CommandSkipMax = 0xDF;
		/** @brief `0xE0`: skip a 16-bit count */
		static constexpr std::uint8_t CommandSkipLong = 0xE0;
		/** @brief `0xE1`: copy a 16-bit count of bytes */
		static constexpr std::uint8_t CommandLiteralLong = 0xE1;
		/** @brief `0xE2`: repeat one byte a 16-bit number of times */
		static constexpr std::uint8_t CommandRunLong = 0xE2;
		/** @brief `0xFF`: no more commands for this frame */
		static constexpr std::uint8_t CommandEndOfFrame = 0xFF;

		/** @brief Longest span a single-byte skip command can express */
		static constexpr std::int32_t MaxShortSkip = CommandSkipMax - CommandSkipBase + 1;
		/** @brief Longest span a single-byte literal command can express */
		static constexpr std::int32_t MaxShortLiteral = CommandLiteralMax + 1;
		/** @brief Longest run a single-byte run command can express */
		static constexpr std::int32_t MaxShortRun = CommandRunMax - CommandRunBase + CommandRunMinLength;
	};
}
