#pragma once

#include "../../Main.h"

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::AssetPacker
{
	/**
		@brief Rewrites the game content of a bootable console disc image

		The two consoles that boot from a read-only disc --- the Sega Dreamcast and the PlayStation 2 --- have
		their content authored into the image at build time, because there is nowhere else on either of them
		to put it. The price is that changing what is on a disc used to mean building the whole image again,
		with the console toolchain and an image authoring tool at hand. This does the other half of the job:
		it takes a finished image apart, keeps the bootstrap and the executable exactly as they are, and
		writes it back out around a different `Content` directory.

		A disc cannot be patched in place: the files are laid out one after another at fixed addresses, so
		anything that changes a single size moves everything after it. The file system is therefore generated
		again from scratch, which is what @ref Jazz2::AssetPacker::Iso9660Builder is for, and only what
		surrounds it is carried over from the image that was opened --- for a DiscJuggler image of a Dreamcast
		disc that is the audio track of the first session, the track layout and the descriptor it ends with,
		and for the plain ISO of a PlayStation 2 disc there is nothing to carry over at all.
	*/
	class DiscImage
	{
	public:
		/** @brief Returns `true` if the file is a DiscJuggler image (`.cdi`), the form a Dreamcast disc is kept in */
		static bool IsDiscJugglerImage(StringView path);

		/**
			@brief Returns `true` if the tilesets already on the disc are in LZ4

			Only a build of the game that decodes LZ4 sprite sheets and tilesets is ever given them, so this tells
			whether new content may use them too. `false` also when the disc has no tilesets to tell by.
		*/
		static bool CarriesLz4Images(StringView path);

		/**
			@brief Writes a copy of a disc image with its `Content` directory replaced

			@param sourcePath	Image to read --- a DiscJuggler `.cdi` or a plain `.iso`
			@param targetPath	Image to write, which may be the same file
			@param contentPath	Directory that becomes `Content` on the disc, whatever it is called here
		*/
		static bool SwapContent(StringView sourcePath, StringView targetPath, StringView contentPath);
	};
}
