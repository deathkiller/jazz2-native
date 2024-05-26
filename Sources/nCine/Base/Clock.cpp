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
#else
#	include <time.h>		// for clock_gettime()
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

	uint64_t Clock::counter() const
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
