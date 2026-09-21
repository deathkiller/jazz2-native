#if defined(WITH_PS2)

#include "Ps2Storage.h"
#include "Ps2Modules.h"
#include "../../../Main.h"

#include <cstring>

#include <Containers/String.h>

// PS2SDK refuses to expose the `fio*` calls to the newlib port without this, on the grounds that mixing
// them with POSIX I/O leads to problems. That warning is about file descriptors - two layers handing out
// their own, neither aware of the other's - and none of that applies to the ONE call used here, which
// opens a directory handle, closes it immediately and never reads through it. It is the acknowledged
// exception rather than an oversight: the POSIX face of this device genuinely cannot answer the question
// (see DirectoryExists() below), which is why the opt-in exists at all.
#define NEWLIB_PORT_AWARE
extern "C" {
#include <kernel.h>
#include <delaythread.h>
#include <fileio.h>
}

using namespace Death::Containers;
using namespace Death::Containers::Literals;

namespace nCine::Backends
{
	namespace
	{
		/**
			@brief Block-device units a filesystem driver may have mounted

			`bdm` hands each partition it recognizes to the filesystem driver as the next free unit, so an
			SD card with a single partition is `mass0:` - but a card partitioned more than once, or a second
			block device the driver also claimed, shifts the rest up. Four covers anything anyone would
			plausibly boot from without turning the probe into a scan.
		*/
		constexpr std::int32_t MaxUnits = 4;

		/**
			@brief How long the drivers are given to find a device, in milliseconds

			`bdm` detects devices from an event thread and the block drivers have to bring the hardware up
			first - `mx4sio_bd` clocks the card through its initialization sequence, `usbmass_bd` waits for
			the host controller to enumerate the bus - so nothing is mounted by the time
			`SifExecModuleBuffer()` returns, and probing immediately finds nothing on a perfectly good device.
			This is the only thing in the startup sequence that waits on hardware that may not be there, so
			the full window is only ever spent when there is nothing at all to find: the first unit to answer
			cuts it short (see @ref MountSettleMs). USB is what sets the length - a stick can take a couple of
			seconds to enumerate where an SD card mounts in well under one - and it is only ever reached by a
			caller that has nowhere else to read from.
		*/
		constexpr std::int32_t MountTimeoutMs = 5000;
		constexpr std::int32_t MountPollIntervalMs = 50;

		/**
			@brief How much longer the probe keeps looking once a first unit has answered, in milliseconds

			The full timeout is a budget for hardware that may not be there at all. Once ONE unit has
			mounted that question is settled, and all that is left is giving `bdm` time to hand over the
			remaining partitions of the same card - which it does within a few of its own event ticks.
		*/
		constexpr std::int32_t MountSettleMs = 250;

		/**
			@brief The earliest the probe may stop once `usbmass_bd` is among the drivers, in milliseconds

			The shortcut above assumes every unit comes from the same device, which stops being true with
			two block drivers loaded: an SD card mounts in a fraction of the time the host controller needs
			to enumerate a bus, so a card that answers first would close the window on a USB stick that was
			about to. Which matters precisely when the card is not the one carrying the game. So the
			shortcut is floored here - long enough for a stick to appear, and still well inside the full
			budget.
		*/
		constexpr std::int32_t UsbEnumerationMs = 2500;

		/** @brief @cpp "mass0:/" @ce and friends, written by the probe and pointed into by @ref _devices */
		char _deviceNames[MaxUnits][8];
		StringView _devices[MaxUnits];
		std::int32_t _deviceCount = 0;

		/**
			@brief The directory part of @cpp argv[0] @ce, kept as a copy

			A copy rather than a view, because the argument vector is the loader's memory and nothing
			promises it outlives the startup that reads it. The length is what an `ioman` path can be
			(`MAXPATHLEN` is 256 there), so anything longer is not a path this console could have opened
			anyway.
		*/
		char _bootDirectory[256];
		std::size_t _bootDirectoryLength = 0;

		/** @brief Records every unit that answers for its own root, and returns how many did */
		std::int32_t CollectMountedDevices()
		{
			_deviceCount = 0;
			for (std::int32_t unit = 0; unit < MaxUnits; unit++) {
				char* name = _deviceNames[_deviceCount];
				name[0] = 'm'; name[1] = 'a'; name[2] = 's'; name[3] = 's';
				name[4] = char('0' + unit); name[5] = ':'; name[6] = '/'; name[7] = '\0';

				if (Ps2Storage::DirectoryExists(StringView(name, 7))) {
					_devices[_deviceCount++] = StringView(name, 7);
				}
			}

			// A driver this code did not load may have registered the device without unit numbers at all:
			// `usbhdfsd`, which every older wLaunchELF carries, answers for "mass:" and for nothing else.
			// Only worth asking when the numbered form found none, because the drivers that do number their
			// units answer for "mass:" as well (as unit 0) and would double every entry.
			if (_deviceCount == 0) {
				char* name = _deviceNames[0];
				name[0] = 'm'; name[1] = 'a'; name[2] = 's'; name[3] = 's';
				name[4] = ':'; name[5] = '/'; name[6] = '\0';

				if (Ps2Storage::DirectoryExists(StringView(name, 6))) {
					_devices[_deviceCount++] = StringView(name, 6);
				}
			}
			return _deviceCount;
		}
	}

	bool Ps2Storage::DirectoryExists(StringView path)
	{
		if (path.empty()) {
			return false;
		}

		// Not `fs::DirectoryExists()`, which cannot answer this question on a FAT volume. That one has to
		// reconstruct a stat() the PS2 cannot be trusted to give (see `HasReliableFileStatus` in
		// FileSystem.cpp), so it opens the path instead - and `bdmfs_fatfs` passes `open()` straight to
		// FatFs `f_open()`, which rejects a directory with FR_NO_FILE and a volume root with
		// FR_INVALID_NAME. Its `opendir()` fallback is no better: PS2SDK's `_open()` ignores O_DIRECTORY
		// and newlib's opendir() is built on open(), so both arrive at the same f_open(). That is only
		// survivable on `cdfs`, whose open() does accept directories.
		//
		// `getstat` would answer (the driver special-cases the volume root), but it reports FIO_S_IFDIR
		// (0x1000) into a struct the EE glue reads with the FIO_SO_* bits (FIO_SO_IFDIR is 0x0020), so the
		// file type is lost on the way up and stat() reports neither a file nor a directory.
		//
		// `dopen` has neither problem: it reaches FatFs `f_opendir()`, which explicitly allows the origin
		// directory itself, and it goes through the same `ioman` the rest of the I/O here already uses
		// (`fioDopen()` binds its own RPC on first call, exactly as `fioOpen()` does).
		//
		// FatFs accepts a volume root written either way, but rejects a deeper path that ends in a
		// separator - its segmenter runs once more after the trailing one and refuses the empty name it
		// gets. Callers build these paths by concatenation, so trimming it is what makes the two agree.
		while (path.size() > 1 && (path[path.size() - 1] == '/' || path[path.size() - 1] == '\\') &&
				path[path.size() - 2] != ':') {
			path = path.exceptSuffix(1);
		}

		auto nullTerminatedPath = String::nullTerminatedView(path);
		const std::int32_t dd = fioDopen(nullTerminatedPath.data());
		if (dd < 0) {
			return false;
		}
		fioDclose(dd);
		return true;
	}

	void Ps2Storage::Initialize()
	{
		static bool attempted = false;
		if (attempted) {
			return;
		}
		attempted = true;

		// Not on a console that booted with a disc in it, whatever that disc turned out to carry. Everything
		// below is the riskiest machinery in the port on hardware that has none of it: a patch written into
		// a running ROM service, and a driver that drives the SIO2 registers itself while `SIO2MAN`, `MCMAN`
		// and the pads are already using them. It exists for the boot that has nowhere else to read from,
		// and that boot is by definition the one with no disc.
		if (Ps2Modules::IsDiscPresent()) {
			LOGI("Booted from a disc, so no removable storage is looked for");
			return;
		}

		// Whatever the loader left mounted is used as it is. A bare executable is started by something -
		// wLaunchELF, Open PS2 Loader - which read it off exactly the kind of device this function goes on
		// to look for, and which therefore already has a block driver resident and talking to that hardware.
		// Loading a SECOND copy of one is not a harmless duplicate: `mx4sio_bd` bit-bangs the SIO2 registers
		// itself, so two instances clock the same card against each other, and the boot stops there with the
		// last thing the console said still on screen. Asking first costs one `fioDopen()` per unit and
		// settles it: the device the game was read from is by definition already mounted.
		if (CollectMountedDevices() > 0) {
			LOGI("The loader left {} unit(s) mounted, the first is \"{}\"", _deviceCount, _devices[0]);
			return;
		}

		// `bdm` is the block-device manager every removable-storage driver registers with and the filesystem
		// driver claims the partitions it hands over, so those two go up first. Without them there is nothing
		// for a block driver to register WITH, which is why they are the only failure that ends this.
		if (!Ps2Modules::Load("bdm.irx"_s, Ps2Modules::Bdm, Ps2Modules::BdmSize) ||
			!Ps2Modules::Load("bdmfs_fatfs.irx"_s, Ps2Modules::BdmfsFatfs, Ps2Modules::BdmfsFatfsSize)) {
			LOGW("The block device manager is unavailable, no removable storage can be mounted");
			return;
		}

		// Both kinds of removable storage are brought up, because the game is as likely to be on a USB stick
		// as on a card in the adapter and nothing here can tell which without looking. They are independent:
		// one driver failing to load leaves the other one's devices perfectly reachable, so neither is
		// allowed to end this the way the two above do. (`usbmass_bd` is the block device, `usbd` is the host
		// controller underneath it, so that pair goes up in that order.)
		bool anyBlockDevice = Ps2Modules::Load("mx4sio_bd.irx"_s, Ps2Modules::Mx4sioBd, Ps2Modules::Mx4sioBdSize);
		const bool usbLoaded = Ps2Modules::Load("usbd.irx"_s, Ps2Modules::Usbd, Ps2Modules::UsbdSize) &&
			Ps2Modules::Load("usbmass_bd.irx"_s, Ps2Modules::UsbmassBd, Ps2Modules::UsbmassBdSize);
		anyBlockDevice |= usbLoaded;
		if (!anyBlockDevice) {
			LOGW("No block device driver could be loaded, no removable storage can be mounted");
			return;
		}

		// The mount happens on the IOP's own threads, so this polls rather than assuming it is already done.
		//
		// It polls to the end of the window even once something has answered, rather than stopping at the
		// first unit: `bdm` hands partitions over one at a time from its event thread, so a card that mounts
		// as more than one unit can easily have `mass0:` up and `mass1:` still coming. Stopping early froze
		// the list at whatever that instant happened to show, and because this function runs at most once
		// nothing ever looked again - a game living on the second partition was simply never found. What the
		// first answer does buy is a shorter window: the driver is clearly alive by then, so the rest of the
		// units only need long enough to be handed over, not the full hardware-detection budget - except
		// that with the USB driver loaded the units may be coming from two devices of very different
		// speeds, which is what @ref UsbEnumerationMs floors the shortened window at.
		const std::int32_t earliestDeadline = (usbLoaded ? UsbEnumerationMs : 0);
		std::int32_t deadline = MountTimeoutMs;
		for (std::int32_t waited = 0; ; waited += MountPollIntervalMs) {
			if (CollectMountedDevices() > 0 && deadline == MountTimeoutMs) {
				deadline = waited + MountSettleMs;
				if (deadline < earliestDeadline) {
					deadline = earliestDeadline;
				}
				if (deadline > MountTimeoutMs) {
					deadline = MountTimeoutMs;
				}
			}
			if (waited >= deadline) {
				break;
			}
			// One call, not `MountPollIntervalMs` of them: `DelayThread()` takes MICROseconds, so this is
			// exactly the interval the loop counts in
			DelayThread(MountPollIntervalMs * 1000);
		}

		if (_deviceCount == 0) {
			LOGI("No removable storage was found on the USB port or in an MX4SIO adapter");
		} else {
			LOGI("Mounted {} unit(s) of removable storage, the first is \"{}\"", _deviceCount, _devices[0]);
		}
	}

	void Ps2Storage::SetBootPath(const char* path)
	{
		_bootDirectoryLength = 0;
		if (path == nullptr) {
			return;
		}

		// The separator is kept, so the result is a directory that concatenates. Both are accepted because
		// both are seen: a loader writes "mass:/APPS/Jazz2/jazz2.elf", but a path typed on the console's own
		// keyboard can just as easily come back with backslashes.
		std::size_t length = 0, directoryLength = 0;
		while (path[length] != '\0') {
			if (length >= sizeof(_bootDirectory)) {
				// Longer than anything `ioman` could open, so it is not a path worth deriving anything from
				return;
			}
			if (path[length] == '/' || path[length] == '\\') {
				directoryLength = length + 1;
			}
			length++;
		}
		if (directoryLength == 0) {
			// A bare file name says nothing about where it came from - there is no working directory on this
			// console to resolve it against
			return;
		}

		std::memcpy(_bootDirectory, path, directoryLength);
		_bootDirectory[directoryLength] = '\0';
		_bootDirectoryLength = directoryLength;
		LOGI("Started from \"{}\"", StringView(path, length));
	}

	StringView Ps2Storage::GetBootDirectory()
	{
		return StringView(_bootDirectory, _bootDirectoryLength);
	}

	ArrayView<const StringView> Ps2Storage::GetMountedDevices()
	{
		return arrayView(_devices, std::size_t(_deviceCount));
	}
}

#endif
