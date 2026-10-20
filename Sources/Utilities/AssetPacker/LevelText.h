#pragma once

#include "../../Main.h"

#include <Containers/StringView.h>

namespace Jazz2::AssetPacker
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::StringView;
#endif

	/**
		@brief Converts a level of the game's own format to text and back, for version control

		The text holds everything the level file does, one thing on a line, so two versions of a level can be compared
		line by line and merged by the usual tools. Converted back, it gives the same level --- the same file byte for
		byte, as @ref Jazz2::Formats::LevelWriter writes it (see @ref FormatVerifier).

		The text starts with a `format` line. Strings are quoted, with C escapes for `"`, `\` and the control
		characters. Floating-point values are written so they're read back exactly, or as `bits:XXXXXXXX` if they
		aren't numbers. A tile is its ID, followed by `:` and its flags in hexadecimal if it has any. Binary data, such
		as the tile sets the level stores in itself or the payload of a chunk, is written in Base64 lines up to `end`.
		Only the events of the grid that aren't empty are written, each with its position. Lines starting with `#` are
		comments.
	*/
	class LevelText
	{
	public:
		LevelText() = delete;

		/** @brief Writes a level as text, returns `false` if it can't be read or the text can't be written */
		static bool LevelToText(StringView levelPath, StringView textPath);
		/** @brief Writes a level from text, returns `false` if the text is damaged or the level can't be written */
		static bool TextToLevel(StringView textPath, StringView levelPath);
	};
}
