#include "Timer.h"
#include "Clock.h"

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
}
