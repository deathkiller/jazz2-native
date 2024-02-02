#include "TimeStamp.h"
#include "../../Common.h"

namespace nCine
{
	TimeStamp::TimeStamp()
		: counter_(0)
	{
	}

	bool TimeStamp::operator>(const TimeStamp& other) const
	{
		return counter_ > other.counter_;
	}

	bool TimeStamp::operator<(const TimeStamp& other) const
	{
		return counter_ < other.counter_;
	}

	TimeStamp& TimeStamp::operator+=(const TimeStamp& other)
	{
		counter_ += other.counter_;
		return *this;
	}

	TimeStamp& TimeStamp::operator-=(const TimeStamp& other)
	{
		ASSERT(counter_ >= other.counter_);
		counter_ -= other.counter_;
		return *this;
	}

	TimeStamp TimeStamp::operator+(const TimeStamp& other) const
	{
		return TimeStamp(counter_ + other.counter_);
	}

	TimeStamp TimeStamp::operator-(const TimeStamp& other) const
	{
		ASSERT(counter_ >= other.counter_);
		return TimeStamp(counter_ - other.counter_);
	}

	TimeStamp TimeStamp::timeSince() const
	{
		return TimeStamp(clock().now() - counter_);
	}

	float TimeStamp::secondsSince() const
	{
		return TimeStamp(clock().now() - counter_).seconds();
	}

	float TimeStamp::millisecondsSince() const
	{
		return TimeStamp(clock().now() - counter_).milliseconds();
	}

	float TimeStamp::microsecondsSince() const
	{
		return TimeStamp(clock().now() - counter_).microseconds();
	}

	float TimeStamp::nanosecondsSince() const
	{
		return TimeStamp(clock().now() - counter_).nanoseconds();
	}

	float TimeStamp::seconds() const
	{
		return static_cast<float>(counter_) / clock().frequency();
	}

	float TimeStamp::milliseconds() const
	{
		return (static_cast<float>(counter_) / clock().frequency()) * 1000.0f;
	}

	float TimeStamp::microseconds() const
	{
		return (static_cast<float>(counter_) / clock().frequency()) * 1000000.0f;
	}

	float TimeStamp::nanoseconds() const
	{
		return (static_cast<float>(counter_) / clock().frequency()) * 1000000000.0f;
	}
}
