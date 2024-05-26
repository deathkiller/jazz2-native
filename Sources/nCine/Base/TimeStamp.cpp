#include "TimeStamp.h"
#include "../../Common.h"

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
		ASSERT(_counter >= other._counter);
		_counter -= other._counter;
		return *this;
	}

	TimeStamp TimeStamp::operator+(const TimeStamp& other) const
	{
		return TimeStamp(_counter + other._counter);
	}

	TimeStamp TimeStamp::operator-(const TimeStamp& other) const
	{
		ASSERT(_counter >= other._counter);
		return TimeStamp(_counter - other._counter);
	}

	TimeStamp TimeStamp::timeSince() const
	{
		return TimeStamp(clock().now() - _counter);
	}

	float TimeStamp::secondsSince() const
	{
		return TimeStamp(clock().now() - _counter).seconds();
	}

	float TimeStamp::millisecondsSince() const
	{
		return TimeStamp(clock().now() - _counter).milliseconds();
	}

	float TimeStamp::microsecondsSince() const
	{
		return TimeStamp(clock().now() - _counter).microseconds();
	}

	float TimeStamp::nanosecondsSince() const
	{
		return TimeStamp(clock().now() - _counter).nanoseconds();
	}

	float TimeStamp::seconds() const
	{
		return static_cast<float>(_counter) / clock().frequency();
	}

	float TimeStamp::milliseconds() const
	{
		return (static_cast<float>(_counter) / clock().frequency()) * 1000.0f;
	}

	float TimeStamp::microseconds() const
	{
		return (static_cast<float>(_counter) / clock().frequency()) * 1000000.0f;
	}

	float TimeStamp::nanoseconds() const
	{
		return (static_cast<float>(_counter) / clock().frequency()) * 1000000000.0f;
	}
}
