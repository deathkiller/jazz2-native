#pragma once

#if defined(WITH_PS2) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <Containers/ArrayView.h>
#include <Containers/StringView.h>

namespace nCine::Backends
{
	/**
		@brief Removable block storage on the PlayStation 2 - an SD card in an **MX4SIO** adapter

		The console has no removable storage a stock kernel can see: the disc is served by `cdfs`, the memory
		card by `MCMAN`, and an SD card on a controller port by nothing at all until three IOP modules are
		resident. This brings those up and reports what mounted, which is the whole of what a platform backend
		can usefully say about it - **where an application keeps its files on the card, and whether it prefers
		them to the disc, is the application's business**, decided in one place there (see
		`ContentResolver::InitializePaths()`).

		The modules are `bdm` (the block device manager every removable-storage driver registers with),
		`bdmfs_fatfs` (FAT12/16/32 and exFAT on top of it) and `mx4sio_bd` (the adapter itself) - all three
		linked into the executable, because a driver that is the only way to reach a device cannot be read
		from it (see @ref Ps2Modules). `bdmfs_fatfs` registers its partitions with the ORIGINAL `ioman`, the
		I/O manager the newlib port's `open()` reaches and the one `cdfs` uses, so a mounted unit is an
		ordinary `mass0:/...` path and needs no special-case I/O anywhere.
	*/
	namespace Ps2Storage
	{
		/**
			@brief Loads the block-device modules and waits briefly for a card to mount

			Idempotent, and cheap to not call: a caller that already has its files elsewhere (a disc) should
			simply not call it, and then no module is loaded and nothing is waited for. It has to run after
			the ROM `SIO2MAN` is up, because the adapter plugs into the same controller port the pads and
			memory cards are on - the driver does not go through `SIO2MAN` itself, the two only share the
			hardware.

			Failing is not an error, it only means @ref GetMountedDevices() stays empty.
		*/
		void Initialize();

		/**
			@brief Mount points of the units that came up, as @cpp "mass0:/" @ce, @cpp "mass1:/" @ce, ...

			Empty until @ref Initialize() has run, and empty afterwards if there was no adapter, no card, or
			no filesystem on it. More than one entry means the card is partitioned more than once (or a second
			block device was claimed as well), so a caller looking for its own files walks all of them.
		*/
		Death::Containers::ArrayView<const Death::Containers::StringView> GetMountedDevices();

		/**
			@brief Whether @p path names a directory on one of these units

			@relativeref{Death::IO,FileSystem::DirectoryExists()} cannot be used on a `mass0:` path and
			silently answers @cpp false @ce for directories that are plainly there, so anything looking for
			its files on a card has to ask through here instead. The reason is a three-way mismatch between
			what the platform can do and what the filesystem driver implements, and it is explained where it
			is worked around, at the top of this function in `Ps2Storage.cpp`.

			Only meaningful for the units @ref GetMountedDevices() reports; a path on `cdfs` or a memory
			card should go through @relativeref{Death::IO,FileSystem::DirectoryExists()} as everywhere else.
		*/
		bool DirectoryExists(Death::Containers::StringView path);
	}
}

#endif
