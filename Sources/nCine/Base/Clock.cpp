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
#	include <time.h> // for clock_gettime()
#	include <sys/time.h> // for gettimeofday()
#endif

namespace nCine
{
	Clock& clock()
	{
		static Clock instance;
		return instance;
	}

	Clock::Clock()
		: frequency_(0UL), baseCount_(0ULL)
	{
#if defined(DEATH_TARGET_WINDOWS)
		LARGE_INTEGER li;
		if (::QueryPerformanceFrequency(&li)) {
			frequency_ = li.LowPart;
			hasPerfCounter_ = true;
		} else {
			frequency_ = 1000L;
		}
#elif defined(DEATH_TARGET_APPLE)
#	if __MAC_10_12
		frequency_ = 1.0e9L;
#	else
		mach_timebase_info_data_t info;
		mach_timebase_info(&info);
		frequency_ = (info.denom * 1.0e9L) / info.numer;
#	endif
#else
		struct timespec resolution;
		if (clock_getres(CLOCK_MONOTONIC, &resolution) == 0) {
			frequency_ = 1.0e9L;
			hasMonotonicClock_ = true;
		} else {
			frequency_ = 1.0e6L;
		}
#endif

		baseCount_ = counter();
	}

	uint64_t Clock::counter() const
	{
		uint64_t counter = 0LL;

#if defined(DEATH_TARGET_WINDOWS)
		if (hasPerfCounter_) {
			::QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&counter));
		} else {
			counter = ::GetTickCount();
		}
#elif defined(DEATH_TARGET_APPLE)
#	if __MAC_10_12
		counter = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
#	else
		counter = mach_absolute_time();
#	endif
#else
		if (hasMonotonicClock_) {
			struct timespec now;
			clock_gettime(CLOCK_MONOTONIC, &now);
			counter = static_cast<uint64_t>(now.tv_sec) * frequency_ + static_cast<uint64_t>(now.tv_nsec);
		} else {
			struct timeval now;
			gettimeofday(&now, nullptr);
			counter = static_cast<uint64_t>(now.tv_sec) * frequency_ + static_cast<uint64_t>(now.tv_usec);
		}
#endif

		return counter;
	}
}
