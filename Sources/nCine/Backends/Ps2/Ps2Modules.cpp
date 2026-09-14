#if defined(WITH_PS2)

#include "Ps2Modules.h"
#include "../../../Main.h"

extern "C" {
#include <kernel.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <iopheap.h>
#include <sbv_patches.h>
}

using namespace Death::Containers;

namespace nCine::Backends::Ps2Modules
{
	bool EnableLoadingFromMemory()
	{
		static bool attempted = false;
		static bool succeeded = false;
		if (attempted) {
			return succeeded;
		}
		attempted = true;

		// `SifExecModuleBuffer()` copies the image into IOP memory it allocates itself and then asks the
		// loadfile service to start it, so both of those have to be bound before the patch below can be
		// written through them (and before any module is loaded from a buffer)
		SifInitIopHeap();
		SifLoadFileInit();

		// The patch the whole thing hangs on - see the header. It is not fatal for it to fail on a console
		// whose LOADFILE already has the entry point (the official return code does not distinguish the two),
		// but there is no way to tell that from here, so a failure disables the embedded modules rather than
		// letting the first RPC bind against a module that was never started spin forever.
		const int lmb = sbv_patch_enable_lmb();
		if (lmb != 0) {
			LOGW("sbv_patch_enable_lmb() failed with {}, the modules carried in the executable cannot be "
				"loaded - there will be no sound and no SD card support", lmb);
			return false;
		}

		// Lifts MODLOAD's list of devices an unsigned executable may be loaded from. Nothing here loads one
		// from a path, but the SD card support below makes the executable itself launchable from one, and a build
		// that has been through this patch is the one such a loader hands control to.
		sbv_patch_disable_prefix_check();

		succeeded = true;
		return true;
	}

	bool Load(StringView name, const std::uint8_t* image, std::uint32_t size)
	{
		if (!EnableLoadingFromMemory()) {
			return false;
		}

		int moduleResult = 0;
		const int moduleId = SifExecModuleBuffer(const_cast<std::uint8_t*>(image), size, 0, nullptr, &moduleResult);
		if (moduleId < 0) {
			LOGE("Cannot load the embedded \"{}\" ({})", name, moduleId);
			return false;
		}

		// `moduleResult` is the module's own `_start()` return, and a non-zero one is not a failure to load.
		// `MODULE_NO_RESIDENT_END` means the module chose not to stay resident, and for two of the four here
		// that is exactly what an ALREADY-RESIDENT copy reports: `bdm.irx` and `audsrv.irx` both leave
		// `_start()` with it when `RegisterLibraryEntries()` says the library is already registered, neither
		// of them releasing it first. Nothing in this port resets the IOP, so a build launched off an SD card
		// is handed a console whose loader necessarily has the whole block-device stack up already - and
		// rejecting that would turn the one boot this mechanism exists for into "no SD card and no sound",
		// on hardware where both are working. The service is there either way, which is all the caller needs.
		// (PS2SDK's own samples do not look at this value at all.)
		constexpr int ModuleNoResidentEnd = 1;
		if (moduleResult == ModuleNoResidentEnd) {
			LOGI("Loaded the embedded \"{}\" = {} (already resident, the running copy is used)", name, moduleId);
		} else {
			LOGI("Loaded the embedded \"{}\" = {}", name, moduleId);
		}
		return true;
	}
}

#endif
