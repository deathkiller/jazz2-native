#include "Clock.h"

#if defined(DEATH_TARGET_WINDOWS)
#	include <winsync.h>
#	include <profileapi.h>
#elif defined(DEATH_TARGET_APPLE)
#	include <Availability.h>
#	if __MAC_10_12
#		include <time.h>
#	else
#		include <mach/mach_time.h>
#	endif
#elif defined(DEATH_TARGET_N64)
#	include <n64sys.h>		// libdragon's COP0 tick counter, this newlib has no clock_gettime()
#else
#	include <time.h>		// for clock_gettime()
#	if defined(DEATH_TARGET_PS3)
#		include <sys/systime.h>	// lv2's clock, this newlib has no clock_gettime()
#	endif
#	include <sys/time.h>	// for gettimeofday()
#endif

namespace nCine
{
	Clock& clock()
	{
		static Clock instance;
		return instance;
	}

	Clock::Clock()
		: _frequency(0UL)
	{
#if defined(DEATH_TARGET_WINDOWS)
		LARGE_INTEGER li;
		if (::QueryPerformanceFrequency(&li)) {
			_frequency = li.LowPart;
			_hasPerfCounter = true;
		} else {
			_frequency = 1000L;
			_hasPerfCounter = false;
		}
#elif defined(DEATH_TARGET_APPLE)
#	if __MAC_10_12
		_frequency = 1.0e9L;
#	else
		mach_timebase_info_data_t info;
		mach_timebase_info(&info);
		_frequency = (info.denom * 1.0e9L) / info.numer;
#	endif
#elif defined(DEATH_TARGET_N64)
		// libdragon's newlib has no clock_gettime() either; the COP0 count register ticks at half the CPU
		// clock (TICKS_PER_SECOND) and libdragon extends it to 64 bits, so it is the monotonic clock here
		_frequency = TICKS_PER_SECOND;
		_hasMonotonicClock = true;
#elif defined(DEATH_TARGET_PS3)
		// The powerpc64-ps3-elf newlib defines _POSIX_TIMERS only for RTEMS, so there is no clock_getres()
		// to ask; lv2's own clock reports nanoseconds and is always there, so the answer is known statically
		_frequency = 1.0e9L;
		_hasMonotonicClock = true;
#else
		struct timespec resolution;
		if (clock_getres(CLOCK_MONOTONIC, &resolution) == 0) {
			_frequency = 1.0e9L;
			_hasMonotonicClock = true;
		} else {
			_frequency = 1.0e6L;
			_hasMonotonicClock = false;
		}
#endif
	}

	std::uint64_t Clock::counter() const
	{
#if defined(DEATH_TARGET_WINDOWS)
		if (_hasPerfCounter) {
			LARGE_INTEGER li;
			::QueryPerformanceCounter(&li);
			return li.QuadPart;
		} else {
			return ::GetTickCount64();
		}
#elif defined(DEATH_TARGET_APPLE)
#	if __MAC_10_12
		return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
#	else
		return mach_absolute_time();
#	endif
#elif defined(DEATH_TARGET_N64)
		return get_ticks();
#elif defined(DEATH_TARGET_PS3)
		std::uint64_t sec = 0, nsec = 0;
		sysGetCurrentTime(&sec, &nsec);
		return sec * static_cast<std::uint64_t>(_frequency) + nsec;
#else
		if (_hasMonotonicClock) {
			struct timespec now;
			clock_gettime(CLOCK_MONOTONIC, &now);
			return static_cast<std::uint64_t>(now.tv_sec) * _frequency + static_cast<std::uint64_t>(now.tv_nsec);
		} else {
			struct timeval now;
			gettimeofday(&now, nullptr);
			return static_cast<std::uint64_t>(now.tv_sec) * _frequency + static_cast<std::uint64_t>(now.tv_usec);
		}
#endif
	}
}
