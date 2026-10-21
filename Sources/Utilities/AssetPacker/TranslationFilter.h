#pragma once

#include "../../Main.h"

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::AssetPacker
{
	/**
		@brief Leaves the texts of one context out of a compiled translation

		The level editor marks all of its texts with the `editor` context, and the consoles that are built without
		the editor (see `WITH_EDITOR`) would only carry them around - well over half of a translation once the editor
		is translated, in a file the game reads whole into memory. A translation is a GNU `.mo` file, a sorted table
		of the original texts, the table of their translations and a hash table to look them up by; leaving entries
		out keeps the order, and the hash table is built again for those that are left.
	*/
	class TranslationFilter
	{
	public:
		/**
			@brief Removes every text of @p context from the `.mo` file at @p path, rewriting it in place

			The file is written as little-endian whatever it was, which the game reads either way, and is not
			touched at all if it has no such text.

			@return Number of texts removed, or `-1` if the file isn't a translation that can be read
		*/
		static std::int32_t RemoveContext(StringView path, StringView context);
	};
}
