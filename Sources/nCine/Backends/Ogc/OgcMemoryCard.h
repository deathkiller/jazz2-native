#pragma once

#if (defined(WITH_OGC) && defined(DEATH_TARGET_GAMECUBE)) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <Containers/StringView.h>

namespace nCine::Backends
{
	/**
		@brief GameCube memory cards, as the devices @cpp "mca:" @ce (slot A) and @cpp "mcb:" @ce (slot B)

		A game started from the disc has nowhere else to write, the way a Dreamcast has its VMUs and a
		PlayStation 2 its memory cards. The card itself is not a file system anyone could mount: libogc's
		`CARD_*` functions create a file of a fixed number of 8 KB blocks, under the game code and maker code of
		the game, and read and write it in whole blocks. These two devices put a file interface over that, so the
		settings and the resumable state are streamed exactly as everywhere else: a file is read whole when it is
		opened, kept in memory while it is open, and written back whole, re-created if it no longer fits, when a
		handle that wrote to it is closed. There are no directories, only the root.

		Each file starts with a short header of its own (a signature, the length of the data and a checksum, so
		a torn write reads as no file rather than as garbage), followed by the comment and the icon the console's
		memory card manager lists it with (see @ref SetNextFileDescription()).

		A slot that holds an SD card adapter or a USB Gecko is not a memory card, and is never touched beyond the
		probe that says so.
	*/
	namespace OgcMemoryCard
	{
		/**
			@brief Registers both devices, under the game ID the disc was made with

			Nothing is read from or written to a card until a path on one of the devices is used.
		*/
		void Initialize(Death::Containers::StringView gameId);

		/** @brief Whether a usable memory card is in the slot, mounting it if it is (0 is slot A, 1 is slot B) */
		bool IsUsable(std::int32_t slot);

		/**
			@brief Sets the comment the next file written to a card is listed with

			The console's memory card manager shows two lines of up to 32 characters next to the icon: the name of
			the game and what the file is. Kept until the next file is written.
		*/
		void SetNextFileDescription(Death::Containers::StringView title, Death::Containers::StringView description);
	}
}

#endif
