#if defined(WITH_PS2)

#include "Ps2Storage.h"
#include "Ps2Modules.h"
#include "../../../Main.h"

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
			@brief How long the driver is given to find a card, in milliseconds

			`bdm` detects devices from an event thread and `mx4sio_bd` has to clock the card through its
			initialization sequence first, so nothing is mounted by the time `SifExecModuleBuffer()` returns -
			probing immediately finds nothing on a perfectly good card. This is the only thing in the startup
			sequence that waits on hardware that may not be there, so it is kept short: a card that is present
			mounts in well under a second, and it is only ever reached by a caller that has nowhere else to
			read from.
		*/
		constexpr std::int32_t MountTimeoutMs = 2000;
		constexpr std::int32_t MountPollIntervalMs = 50;

		/**
			@brief How much longer the probe keeps looking once a first unit has answered, in milliseconds

			The full timeout is a budget for hardware that may not be there at all. Once ONE unit has
			mounted that question is settled, and all that is left is giving `bdm` time to hand over the
			remaining partitions of the same card - which it does within a few of its own event ticks.
		*/
		constexpr std::int32_t MountSettleMs = 250;

		/** @brief @cpp "mass0:/" @ce and friends, written by the probe and pointed into by @ref _devices */
		char _deviceNames[MaxUnits][8];
		StringView _devices[MaxUnits];
		std::int32_t _deviceCount = 0;

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

		// `bdm` is the block-device manager every removable-storage driver registers with, the filesystem
		// driver claims the partitions it hands over, and the MX4SIO driver is the block device itself - so
		// they go up in that order, and a failure at any step just means there is nothing to mount.
		if (!Ps2Modules::Load("bdm.irx"_s, Ps2Modules::Bdm, Ps2Modules::BdmSize) ||
			!Ps2Modules::Load("bdmfs_fatfs.irx"_s, Ps2Modules::BdmfsFatfs, Ps2Modules::BdmfsFatfsSize) ||
			!Ps2Modules::Load("mx4sio_bd.irx"_s, Ps2Modules::Mx4sioBd, Ps2Modules::Mx4sioBdSize)) {
			LOGW("The MX4SIO block device is unavailable, no removable storage can be mounted");
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
		// units only need long enough to be handed over, not the full hardware-detection budget.
		std::int32_t deadline = MountTimeoutMs;
		for (std::int32_t waited = 0; ; waited += MountPollIntervalMs) {
			if (CollectMountedDevices() > 0 && deadline == MountTimeoutMs) {
				deadline = (waited + MountSettleMs < MountTimeoutMs ? waited + MountSettleMs : MountTimeoutMs);
			}
			if (waited >= deadline) {
				break;
			}
			// One call, not `MountPollIntervalMs` of them: `DelayThread()` takes MICROseconds, so this is
			// exactly the interval the loop counts in
			DelayThread(MountPollIntervalMs * 1000);
		}

		if (_deviceCount == 0) {
			LOGI("No MX4SIO card was found");
		} else {
			LOGI("MX4SIO mounted {} unit(s), the first is \"{}\"", _deviceCount, _devices[0]);
		}
	}

	ArrayView<const StringView> Ps2Storage::GetMountedDevices()
	{
		return arrayView(_devices, std::size_t(_deviceCount));
	}
}

#endif
