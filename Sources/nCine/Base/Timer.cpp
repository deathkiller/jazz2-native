#include "Timer.h"
#include "Clock.h"

#include <CommonWindows.h>

#if defined(DEATH_TARGET_EMSCRIPTEN)
#	include <emscripten/emscripten.h>
#elif defined(DEATH_TARGET_WINDOWS)
#	include <synchapi.h>
#else
#	include <unistd.h>
#	if defined(DEATH_TARGET_SWITCH)
#		include <switch.h>
#	endif
#endif

namespace nCine
{
	Timer::Timer()
		: isRunning_(false), startTime_(clock().now()), accumulatedTime_(0ULL)
	{
	}

	void Timer::start()
	{
		isRunning_ = true;
		startTime_ = clock().counter();
	}

	void Timer::stop()
	{
		accumulatedTime_ += clock().counter() - startTime_;
		isRunning_ = false;
	}

	float Timer::interval() const
	{
		return static_cast<float>(clock().counter() - startTime_) / clock().frequency();
	}

	float Timer::total() const
	{
		return isRunning_
			? static_cast<float>(accumulatedTime_ + clock().counter() - startTime_) / clock().frequency()
			: static_cast<float>(accumulatedTime_) / clock().frequency();
	}

	void Timer::sleep(std::uint32_t milliseconds)
	{
#if defined(DEATH_TARGET_EMSCRIPTEN)
		emscripten_sleep(milliseconds);
#elif defined(DEATH_TARGET_SWITCH)
		const std::int64_t nanoseconds = static_cast<std::int64_t>(milliseconds) * 1000000;
		svcSleepThread(nanoseconds);
#elif defined(DEATH_TARGET_WINDOWS)
		::SleepEx(static_cast<DWORD>(milliseconds), FALSE);
#else
		const unsigned int microseconds = static_cast<unsigned int>(milliseconds) * 1000;
		::usleep(microseconds);
#endif
	}
}
