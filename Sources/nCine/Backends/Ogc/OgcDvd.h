#pragma once

#if (defined(WITH_OGC) && defined(DEATH_TARGET_GAMECUBE)) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <Containers/StringView.h>

namespace nCine::Backends
{
	/**
		@brief The GameCube disc, mounted read-only as @cpp "dvd:" @ce

		The disc image `AssetPacker create-image` writes is an ordinary ISO 9660 volume whose system area holds
		the GameCube disc header and apploader (see `DiscImage.h` in the tool), so the game reads its content
		through the ISO 9660 hierarchy, with the Joliet names where the volume has them. libogc ships a driver
		for exactly that (`ISO9660_Mount()` over `__io_gcdvd`), but it is not used: it leaks the whole listing
		of the parent directory on every `open()` and `stat()`, which is tens of kilobytes per level load on a
		console with 24 MB, and its startup resets the drive, which costs more than a second and is not needed
		when the console booted from the very disc being read. This one reads every directory once and keeps it,
		and reads the files through a read-ahead cache of its own.

		Only the disc of this game is mounted: the probe compares the game ID in the disc header with the one
		the image was made with, so a retail disc left in the drive of a console that booted the game from an
		SD card is never mistaken for anything.
	*/
	namespace OgcDvd
	{
		/**
			@brief Mounts the disc as @cpp "dvd:" @ce if it is the one made for this game

			@param allowDriveReset	Whether the drive may be reset and spun up when it does not answer as it
				is, which is what it takes when the console booted from something other than the disc - more
				than a second, and pointless whenever the content can be found elsewhere

			Idempotent. Returns @cpp false @ce when there is no disc, a different disc, or no drive that
			answers in time.
		*/
		bool Mount(Death::Containers::StringView gameId, bool allowDriveReset);

		/** @brief Whether @ref Mount() has succeeded */
		bool IsMounted();
	}
}

#endif
