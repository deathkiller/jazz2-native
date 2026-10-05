#pragma once

#include "../../Main.h"

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::AssetPacker
{
	/**
		@brief Rewrites the game content of a bootable console disc image

		The consoles that boot from a read-only disc --- the Sega Dreamcast, the PlayStation 2 and the GameCube
		--- have their content authored into the image at build time, because there is nowhere else on any of
		them to put it. The price is that changing what is on a disc used to mean building the whole image
		again, with the console toolchain and an image authoring tool at hand. This does the other half of the
		job: it takes a finished image apart, keeps the bootstrap and the executable exactly as they are, and
		writes it back out around a different `Content` directory.

		A disc cannot be patched in place: the files are laid out one after another at fixed addresses, so
		anything that changes a single size moves everything after it. The file system is therefore generated
		again from scratch, which is what @ref Jazz2::AssetPacker::Iso9660Builder is for, and only what
		surrounds it is carried over from the image that was opened --- for a DiscJuggler image of a Dreamcast
		disc that is the audio track of the first session, the track layout and the descriptor it ends with,
		for the plain ISO of a PlayStation 2 disc there is nothing to carry over at all, and a GameCube disc
		keeps its header and apploader in the system area, with the header pointed at wherever the executable
		ends up.

		The GameCube image is the one kind this also creates (@ref CreateGameCubeImage()): no image authoring
		tool knows its header, which is what the console's boot ROM reads before anything else. It is an
		ordinary ISO 9660 volume whose system area --- the first 32 KB, which the file system leaves alone ---
		holds the disc header, the `bi2.bin` block and the apploader, exactly where a GameCube disc has them,
		and the executable is one of the files of the volume, `Content` the directory next to it. Everything
		on a computer reads it as the ISO it is, the console and Dolphin as the disc it also is, and the game
		reads its content through the ISO 9660 hierarchy (see `OgcDvd.cpp`).
	*/
	class DiscImage
	{
	public:
		/** @brief What a new GameCube disc image is made of, see @ref CreateGameCubeImage() */
		struct GameCubeImageDescription {
			/** @brief The executable, a `.dol` */
			StringView ExecutablePath;
			/** @brief The apploader the boot ROM runs to load the executable, with the 32-byte header in front of its code */
			StringView ApploaderPath;
			/** @brief Directory that becomes `Content` on the disc, whatever it is called here */
			StringView ContentPath;
			/** @brief Six characters: the game code --- console, two of its own and the region --- and the maker code */
			StringView GameId;
			/** @brief Name of the game, which only the disc header carries */
			StringView Title;
		};

		/** @brief Returns `true` if the file is a DiscJuggler image (`.cdi`), the form a Dreamcast disc is kept in */
		static bool IsDiscJugglerImage(StringView path);

		/** @brief Returns `true` if the file is a GameCube disc image, which is a plain `.iso` that starts with the disc header */
		static bool IsGameCubeImage(StringView path);

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

		/** @brief Writes a new bootable GameCube disc image */
		static bool CreateGameCubeImage(const GameCubeImageDescription& description, StringView targetPath);
	};
}
