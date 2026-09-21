#pragma once

#if defined(WITH_PS2) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <cstdint>

#include <Containers/StringView.h>

namespace nCine::Backends
{
	/**
		@brief PS2SDK's IOP modules, carried inside the executable rather than read from storage

		Everything the Emotion Engine cannot do itself is done by an IRX running on the I/O Processor, and the
		services this port needs from it - the SPU2, and removable storage on either a controller port
		(MX4SIO) or the USB port - are not in ROM. They have to be loaded, and loading one is a
		chicken-and-egg problem: the disc's own driver (`CDFS.IRX`) can be read off the disc because
		`SifLoadModule()` goes through the IOP's loadfile service rather than the EE's file I/O, but a module
		that is the ONLY way to reach a storage device obviously cannot be read from that device. The MX4SIO
		and USB drivers are exactly that case, and when the game boots from one of those there is no disc
		either, so `audsrv` would be unreachable too.

		So they are linked in as byte arrays (see `cmake/ncine_ps2_embed_irx.cmake`, which turns the `.irx`
		files from `$PS2SDK/iop/irx` into a generated source at configure time) and handed to
		`SifExecModuleBuffer()`. All of them together are about 110 KB of a 3.5 MB executable, which also buys
		a boot with no module seeks on the disc at all.

		Loading order matters and is the loader's business, not this header's - see `Ps2Storage::Initialize()`
		for the block-device stack and `Ps2AudioDevice::InitializeModules()` for the sound one.
	*/
	namespace Ps2Modules
	{
		/**
			@brief Records whether the console booted with a disc in the drive

			Called once from `MainApplication`, from the only place that can know. Everything below reads it
			rather than asking the drive again, so the answer is the one the startup sequence actually acted
			on. @ref Load() prefers the disc where there is one, and `Ps2Storage` does not go looking for
			removable storage at all.
		*/
		void SetDiscPresent(bool present);
		/** @brief What @ref SetDiscPresent() was told; `false` until it has been called */
		bool IsDiscPresent();

		/**
			@brief Makes the IOP able to load a module out of EE memory at all

			`SifExecModuleBuffer()` does not work on a stock console: the ROM's `LOADFILE` service has no
			LoadModuleBuffer entry point, and without it the call returns an error - and, worse, whatever the
			module was going to register never appears, so the first RPC bind against it spins forever (that
			is exactly how `audsrv_init()` hangs). The standard answer, which every homebrew loader applies,
			is PS2SDK's two SBV patches: one adds the missing entry point, the other lifts MODLOAD's
			whitelist of devices an unsigned executable may be loaded from.

			Runs at most once and remembers the outcome. When it fails NOTHING below is attempted - a build
			that cannot load its modules loses its sound and its SD card, which is recoverable, where a hang
			is not.

			@returns `false` if the patch could not be applied, which disables every embedded module
		*/
		bool EnableLoadingFromMemory();

		/**
			@brief Loads one of the modules below, reporting what went wrong if it fails

			Succeeding means the service is available, which is not quite the same as "this image is now
			running": a module that was already resident refuses to start a second copy, and that counts as
			success here because the caller gets what it asked for either way (see the implementation).

			@param name   Only for the log line
			@param image  One of the arrays below
			@param size   Its matching size
		*/
		bool Load(Death::Containers::StringView name, const std::uint8_t* image, std::uint32_t size);

		/**
			@brief Loads a module from the disc where there is one, and out of the executable where there is not

			The embedded path exists for the boot that has no disc to read from, and it is the one that needs
			@ref EnableLoadingFromMemory() - a patch written into a running ROM service, on a console whose
			IOP this port deliberately never resets. A disc boot has no need of any of that: `SifLoadModule()`
            off `cdrom0:` is how this port has always loaded `CDFS.IRX`, and it is left the only mechanism on
			that path so that the boot which works keeps working.

			@param name       Only for the log line
			@param discPath   Where the packaging staged it, e.g. @cpp "cdrom0:\\AUDSRV.IRX;1" @ce
			@param image      The embedded fallback
			@param size       Its matching size
		*/
		bool Load(Death::Containers::StringView name, const char* discPath, const std::uint8_t* image, std::uint32_t size);

		/** @brief `audsrv.irx` - the SPU2 streaming server the audio backend talks to (needs `rom0:LIBSD`) */
		extern const std::uint8_t Audsrv[];
		extern const std::uint32_t AudsrvSize;

		/** @brief `bdm.irx` - the block device manager every removable-storage driver registers with */
		extern const std::uint8_t Bdm[];
		extern const std::uint32_t BdmSize;

		/**
			@brief `bdmfs_fatfs.irx` - the FAT12/16/32 and exFAT filesystem over `bdm`

			Registers its partitions as `mass0:`, `mass1:`, ... with the ORIGINAL `ioman` - the same I/O
			manager the newlib port's `open()` reaches, and the one `cdfs` registers with - so an SD card is
			an ordinary path. It is not quite "no special-case I/O anywhere": FatFs cannot open a directory,
			which is what `Ps2Storage::DirectoryExists()` exists to work around. (`bdmfs_vfat.irx` would do
			as well and is smaller, but it stops at FAT32, and the large cards these adapters are used with
			are usually exFAT.)
		*/
		extern const std::uint8_t BdmfsFatfs[];
		extern const std::uint32_t BdmfsFatfsSize;

		/**
			@brief `mx4sio_bd.irx` - the SD card in an MX4SIO adapter, as a `bdm` block device

			Drives the SIO2 port the adapter plugs into directly (its import table names `dmacman` and
			`intrman`, not `sio2man`), so it neither needs nor replaces the ROM `SIO2MAN` the pads and the
			memory card go through - it can simply be loaded alongside them.
		*/
		extern const std::uint8_t Mx4sioBd[];
		extern const std::uint32_t Mx4sioBdSize;

		/** @brief `usbd.irx` - the OHCI host controller behind the console's two USB ports */
		extern const std::uint8_t Usbd[];
		extern const std::uint32_t UsbdSize;

		/**
			@brief `usbmass_bd.irx` - a USB mass-storage device, as a `bdm` block device

			The other half of the pair above, and the reason both are carried: a USB stick is what most of
			these consoles are actually loaded from, it mounts through the same `bdm` stack the MX4SIO card
			does, and the game is as likely to be sitting on one as on a card. Enumerating the bus takes
			the host controller a moment longer than clocking an SD card does, which is what the mount
			window in `Ps2Storage::Initialize()` is sized for.
		*/
		extern const std::uint8_t UsbmassBd[];
		extern const std::uint32_t UsbmassBdSize;
	}
}

#endif
