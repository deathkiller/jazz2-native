#include "TimeStamp.h"
#include "../../Main.h"

namespace nCine
{
	TimeStamp::TimeStamp()
		: _counter(0)
	{
	}

	TimeStamp::TimeStamp(std::uint64_t counter)
		:_counter(counter)
	{
	}

	bool TimeStamp::operator>(const TimeStamp& other) const
	{
		return _counter > other._counter;
	}

	bool TimeStamp::operator<(const TimeStamp& other) const
	{
		return _counter < other._counter;
	}

	TimeStamp& TimeStamp::operator+=(const TimeStamp& other)
	{
		_counter += other._counter;
		return *this;
	}

	TimeStamp& TimeStamp::operator-=(const TimeStamp& other)
	{
		DEATH_ASSERT(_counter >= other._counter);
		_counter -= other._counter;
		return *this;
	}

	TimeStamp TimeStamp::operator+(const TimeStamp& other) const
	{
		return TimeStamp(_counter + other._counter);
	}

	TimeStamp TimeStamp::operator-(const TimeStamp& other) const
	{
		DEATH_ASSERT(_counter >= other._counter);
		return TimeStamp(_counter - other._counter);
	}

	TimeStamp TimeStamp::GetTimeSince() const
	{
		return TimeStamp(clock().Now() - _counter);
	}

	float TimeStamp::GetSecondsSince() const
	{
		return TimeStamp(clock().Now() - _counter).GetSeconds();
	}

	float TimeStamp::GetMillisecondsSince() const
	{
		return TimeStamp(clock().Now() - _counter).GetMilliseconds();
	}

	float TimeStamp::GetMicrosecondsSince() const
	{
		return TimeStamp(clock().Now() - _counter).GetMicroseconds();
	}

	float TimeStamp::GetNanosecondsSince() const
	{
		return TimeStamp(clock().Now() - _counter).GetNanoseconds();
	}

	float TimeStamp::GetSeconds() const
	{
		return static_cast<float>(_counter) / clock().GetFrequency();
	}

	float TimeStamp::GetMilliseconds() const
	{
		return (static_cast<float>(_counter) / clock().GetFrequency()) * 1000.0f;
	}

	float TimeStamp::GetMicroseconds() const
	{
		return (static_cast<float>(_counter) / clock().GetFrequency()) * 1000000.0f;
	}

	float TimeStamp::GetNanoseconds() const
	{
		return (static_cast<float>(_counter) / clock().GetFrequency()) * 1000000000.0f;
	}
}
