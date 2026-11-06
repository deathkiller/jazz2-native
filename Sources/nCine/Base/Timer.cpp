#include "Timer.h"
#include "Clock.h"

namespace nCine
{
	Timer::Timer()
		: _isRunning(false), _startTime(clock().Now()), _accumulatedTime(0ULL)
	{
	}

	void Timer::Start()
	{
		_isRunning = true;
		_startTime = clock().GetCounter();
	}

	void Timer::Stop()
	{
		_accumulatedTime += clock().GetCounter() - _startTime;
		_isRunning = false;
	}

	float Timer::GetInterval() const
	{
		return static_cast<float>(clock().GetCounter() - _startTime) / clock().GetFrequency();
	}

	float Timer::GetTotal() const
	{
		return _isRunning
			? static_cast<float>(_accumulatedTime + clock().GetCounter() - _startTime) / clock().GetFrequency()
			: static_cast<float>(_accumulatedTime) / clock().GetFrequency();
	}
}
