#pragma once

#include "../../Main.h"

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::AssetPacker
{
	/**
		@brief Rewrites the game content of a Nintendo 64 ROM image

		The cartridge counterpart of @ref DiscImage: a `.z64` built by the Nintendo 64 target carries the game
		content in a DragonFS image appended behind the executable, found at run time through the table of
		contents libdragon's `n64tool` writes after the bootcode. This replaces that file system with one built
		from a different `Content` directory and keeps everything ahead of it --- the header (with the save type
		and accessories `ed64romconfig` marked in it), the bootcode, the compressed executable and its symbol
		table --- byte for byte, so a ROM can be given new game data without the libdragon toolchain.

		The directory is staged the way the build stages it (see `cmake/n64_stage_content.cmake`): files the
		console cannot play are left out, and a `Source.pak` becomes `Prebaked.pak`. Music, streamed sound
		effects and video cinematics come from a tree prepared by `convert --target=n64`; a tree of the plain
		`console` profile still plays, with its sound effects taken from the package, no music, and the
		cinematics only if they were re-encoded for this console.
	*/
	class CartridgeImage
	{
	public:
		/** @brief Returns `true` if the file is a Nintendo 64 ROM image in the big-endian (`.z64`) byte order */
		static bool IsCartridgeImage(StringView path);

		/**
			@brief Writes a copy of a ROM image with its content replaced

			@param sourcePath	ROM image to read (`.z64`)
			@param targetPath	ROM image to write, which may be the same file (or empty for the same file)
			@param contentPath	Directory that becomes `rom:/Content`, whatever it is called here
		*/
		static bool SwapContent(StringView sourcePath, StringView targetPath, StringView contentPath);
	};
}
