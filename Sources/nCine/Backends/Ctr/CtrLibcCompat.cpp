#if defined(WITH_CTR)

/*
	The handful of C library functions the engine calls that devkitARM's newlib declares but nothing on
	this console implements - the 3DS counterpart of PspLibcCompat.cpp, and a much shorter one. The pthread
	API itself is NOT among them: devkitARM ships a complete implementation in libsysbase (on every link line
	through 3dsx.specs), built over the `__libc_lock_*` / `__libc_cond_*` hooks libctru fills with its
	LightLock and CondVar, so the engine's Thread and ThreadSync classes link against it unchanged.
*/

#include <sched.h>

extern "C"
{
#include <3ds/types.h>
#include <3ds/svc.h>

	// <sched.h> declares it (the engine's Thread::YieldExecution() calls it), libsysbase has no implementation.
	// A zero-length sleep hands the core to the next ready thread of the same priority, which is what a yield
	// is on this kernel.
	int sched_yield(void)
	{
		svcSleepThread(0);
		return 0;
	}
}

#endif
