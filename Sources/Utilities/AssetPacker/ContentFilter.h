#pragma once

#include "../../Main.h"

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::AssetPacker
{
	/**
		@brief Decides what of a tree of game content is left out of what is made for a target

		The rules `convert` copies the game's own content by, and `swap-content` and `create-image` put a tree on a
		disc or into a ROM by, kept in one place so all of them leave out the same:
		-   `.po` files, the sources of the translations --- the game reads only the compiled `.mo` files
		-   @ref N64TempDirectory at the top, which a failed conversion for the Nintendo 64 leaves behind
		-   `Editor` at the top, what only the level editor reads (its font), for a build without the editor

		What a single console can't use, such as the original music formats on the Nintendo 64, is decided where
		the content is made for that console. `cmake/n64_stage_content.cmake` stages a ROM in the build by the same
		rules, the staging of the other consoles without the editor in `cmake/ncine_extra_sources.cmake` leaves out
		`Editor` too.
	*/
	class ContentFilter
	{
	public:
		/** @brief Directory a conversion for the Nintendo 64 works in, at the top of the tree it makes */
		static constexpr char N64TempDirectory[] = ".n64-temp";

		/**
			@brief Returns `true` if an entry of a content tree is left out

			@param name			Name of the file or the directory
			@param isDirectory	Whether it's a directory
			@param isTopLevel	Whether it's right in the root of the tree
			@param hasEditor	Whether the build the tree is for has the level editor
		*/
		static bool IsLeftOut(StringView name, bool isDirectory, bool isTopLevel, bool hasEditor);
	};
}
