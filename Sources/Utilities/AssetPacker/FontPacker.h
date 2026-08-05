#pragma once

#include "../../Main.h"

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::AssetPacker
{
	/**
		@brief Converts bitmap fonts between the form they are authored in and the one the game loads

		A font is authored as a grid image - one equally sized cell per character - next to a description listing
		the cell size, the character set and how far the pen moves for each. The game instead loads a single file
		in which every glyph has been measured down to the pixels it actually inks, packed against its neighbours
		and stored as palette indices; see @ref Jazz2::UI::FontFormat.

		The description comes in two interchangeable forms: a JSON document (`<image>.png.json`), which is the
		one meant to be edited and the one @ref Pack prefers, and the binary form (`<image>.png.font`) the
		game's own content has always been kept in, which is read when there is no JSON next to the image.
		@ref Unpack writes both.

		Both directions are provided, so a shipped font can be opened up, changed and packed again without the
		grid form having to be kept around. @ref ApplyPalette and @ref ConvertToIndices are the two halves of
		making that grid editable: the atlas holds palette indices, which look like near-black noise in an image
		editor until the palette is applied, and have to be resolved back afterwards.
	*/
	class FontPacker
	{
	public:
		/** @brief Packs a grid image and its character list into the file the game loads */
		static bool Pack(StringView sourcePath, StringView targetPath);
		/** @brief Unpacks a packed font back into a grid image and both forms of its character list */
		static bool Unpack(StringView sourcePath, StringView targetPath);
		/** @brief Replaces the palette indices of an image with the colors they stand for */
		static bool ApplyPalette(StringView sourcePath, StringView targetPath);
		/** @brief Resolves the colors of an image back to the nearest palette indices */
		static bool ConvertToIndices(StringView sourcePath, StringView targetPath);
	};
}
