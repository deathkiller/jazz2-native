#pragma once

#include "../../Main.h"

#include <Containers/StringView.h>
#include <IO/MemoryStream.h>
#include <IO/Stream.h>

using namespace Death::Containers;
using namespace Death::IO;

namespace Jazz2::AssetPacker
{
	/**
		@brief Re-lays out the hand-made sprite sheets that a platform would have to split

		The sheets the conversion produces never have a frame lying across a page line (see
		@ref Compatibility::JJ2Anims::SheetPageSize), but the game's own content also carries sheets drawn by
		hand as a regular grid of cells, laid out however they were authored. A grid wider or taller than one
		page puts cells across the line, and a platform that splits the texture into pages draws those frames
		cut off at it - the water shield and Lori's end-of-level animation among them.

		Such a sheet is rewritten into the tightly packed form the conversion uses: every frame trimmed to the
		pixels it actually covers and laid out by @ref Compatibility::JJ2Anims::PackRectangles(). Each frame
		keeps its place within its cell, so it is drawn exactly where it was.

		Only multi-frame grids larger than a page are touched. The smaller ones are left as they were authored,
		because some are sampled as a whole texture rather than frame by frame - the fire and lightning shields
		scroll across all of theirs - and rewriting them would change what they draw.
	*/
	class SpriteRepacker
	{
	public:
		/**
			@brief Rewrites a grid sheet larger than one page into a tightly packed one

			Returns `true` when the sheet in @p input was rewritten into @p output. Otherwise nothing is
			written and the sheet is to be used as it is - it is not a sprite sheet, it is packed already, it
			fits into a page or it has a single frame. @p name is only used in messages.
		*/
		static bool TryRepack(Stream& input, MemoryStream& output, StringView name);
	};
}
