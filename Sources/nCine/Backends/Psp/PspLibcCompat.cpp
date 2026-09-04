#include "../../../Main.h"

#if defined(DEATH_TARGET_PSP)

#include <sys/time.h>
#include <psprtc.h>

// Replaces pspdev's libcglue `_gettimeofday()`, which everything in newlib that tells the time ends up in:
// `gettimeofday()`, `time()` and through them `std::chrono::system_clock`, and mbedTLS's check of a
// certificate's validity period. The toolchain's own returns the time OF DAY only - seconds since midnight,
// measured 44646 at 12:24 UTC - so the C library believes it is the 1st of January 1970 and every
// certificate issued since is "not yet valid", which is how HTTPS verification failed on this console
// (the libcurl in pspdev, 7.64.1, reports that verdict without a word of detail).
//
// The RTC's tick is microseconds since 0001-01-01 in UTC; 62135596800 is the seconds from there to the
// Unix epoch. Defined in the executable rather than patched in the toolchain, so the fix travels with the
// game: the linker takes this definition and never pulls libcglue's object for the symbol, whose only
// content it is.
extern "C" int _gettimeofday(struct timeval* tv, void* tz)
{
	static_cast<void>(tz);
	if (tv == nullptr) {
		return -1;
	}

	u64 tick = 0;
	if (sceRtcGetCurrentTick(&tick) < 0) {
		tv->tv_sec = 0;
		tv->tv_usec = 0;
		return -1;
	}

	constexpr u64 SecondsToUnixEpoch = 62135596800ULL;
	const u64 seconds = tick / 1000000ULL;
	tv->tv_sec = (time_t)(seconds > SecondsToUnixEpoch ? seconds - SecondsToUnixEpoch : 0);
	tv->tv_usec = (suseconds_t)(tick % 1000000ULL);
	return 0;
}

#endif
