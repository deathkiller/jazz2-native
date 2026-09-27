#include "PspPower.h"

#if defined(DEATH_TARGET_PSP)

#include <psppower.h>
#include <pspthreadman.h>

#include <atomic>

namespace nCine::Backends
{
	namespace
	{
		std::atomic<bool> _suspended{false};
		std::atomic<std::uint32_t> _resumeCount{0};

		int PowerCallback(int arg1, int powerInfo, void* common)
		{
			static_cast<void>(arg1); static_cast<void>(common);

			if ((powerInfo & PSP_POWER_CB_SUSPENDING) != 0) {
				_suspended.store(true, std::memory_order_release);
			} else if ((powerInfo & PSP_POWER_CB_RESUME_COMPLETE) != 0) {
				_resumeCount.fetch_add(1, std::memory_order_acq_rel);
				_suspended.store(false, std::memory_order_release);
			}
			return 0;
		}
	}

	void PspPower::RegisterCallback()
	{
		const SceUID callbackId = sceKernelCreateCallback("nCinePowerCallback", PowerCallback, nullptr);
		if (callbackId >= 0) {
			// -1 takes the first free slot rather than one that a plugin could already be using
			scePowerRegisterCallback(-1, callbackId);
		}
	}

	bool PspPower::IsSuspended()
	{
		return _suspended.load(std::memory_order_acquire);
	}

	std::uint32_t PspPower::GetResumeCount()
	{
		return _resumeCount.load(std::memory_order_acquire);
	}
}

#endif
