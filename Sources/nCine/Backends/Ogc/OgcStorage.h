#pragma once

#if (defined(WITH_OGC) && defined(DEATH_TARGET_GAMECUBE)) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <Containers/ArrayView.h>
#include <Containers/StringView.h>

namespace nCine::Backends
{
	/**
		@brief Storage of the GameCube that is not the disc --- the SD cards in a memory-card slot or in the
			serial port under the console

		libfat mounts every FAT volume it finds as a device of its own: an **SD2SP2** adapter in serial port 2 as
		@cpp "sd:" @ce, and an **SD Gecko** in memory-card slot A or B as @cpp "carda:" @ce or @cpp "cardb:" @ce.
		This mounts them and reports which came up, and where the loader found the executable, which is all a
		platform backend can usefully say about them --- **where the game keeps its files, and whether it prefers
		them to the disc, is decided in `ContentResolver::InitializePaths()`**. The disc itself is
		@ref OgcDvd, the memory cards are @ref OgcMemoryCard.
	*/
	namespace OgcStorage
	{
		/**
			@brief Mounts the FAT volumes and records the path the executable was loaded from

			@param bootPath		The first argument the executable was started with, or @cpp nullptr @ce ---
				a loader started from an SD card passes the path of the executable there, a console that booted
				the disc passes nothing
		*/
		void Initialize(const char* bootPath);

		/** @brief Mount points of the FAT volumes that came up, as @cpp "sd:/" @ce, @cpp "carda:/" @ce, ... */
		Death::Containers::ArrayView<const Death::Containers::StringView> GetMountedDevices();

		/**
			@brief The directory the executable was loaded from, with its trailing separator

			Empty when no path was passed, or when it is not on one of @ref GetMountedDevices().
		*/
		Death::Containers::StringView GetBootDirectory();

		/** @brief Game ID of the disc made for this build, see @ref NCINE_GAMECUBE_GAME_ID */
		Death::Containers::StringView GetGameId();

		/**
			@brief Shows a message on the boot console and stops there

			For the one failure the game cannot start past - no content anywhere it looks. The boot console is
			still on the screen at that point, the renderer does not exist yet.
		*/
		[[noreturn]] void HaltWithMessage(const char* message);
	}
}

#endif
