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
		: _isRunning(false), _startTime(clock().now()), _accumulatedTime(0ULL)
	{
	}

	void Timer::start()
	{
		_isRunning = true;
		_startTime = clock().counter();
	}

	void Timer::stop()
	{
		_accumulatedTime += clock().counter() - _startTime;
		_isRunning = false;
	}

	float Timer::interval() const
	{
		return static_cast<float>(clock().counter() - _startTime) / clock().frequency();
	}

	float Timer::total() const
	{
		return _isRunning
			? static_cast<float>(_accumulatedTime + clock().counter() - _startTime) / clock().frequency()
			: static_cast<float>(_accumulatedTime) / clock().frequency();
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
