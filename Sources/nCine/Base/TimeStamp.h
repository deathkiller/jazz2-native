#pragma once

#include "Clock.h"

namespace nCine
{
	/**
		@brief Wraps a clock counter value representing a point in time or a duration
		
		Stores a raw counter value obtained from the system @ref Clock. The same type is used both for
		absolute time points (e.g. @ref now()) and for durations (e.g. the result of subtracting two
		time stamps). Conversion methods are provided to read the stored value, or the time elapsed
		since it, in seconds, milliseconds, microseconds or nanoseconds.
	*/
	class TimeStamp
	{
	public:
		TimeStamp();

		/** @brief Returns a new time stamp initialized to the current clock value */
		inline static TimeStamp now() {
			return TimeStamp(clock().now());
		}

		bool operator>(const TimeStamp& other) const;
		bool operator<(const TimeStamp& other) const;
		TimeStamp& operator+=(const TimeStamp& other);
		TimeStamp& operator-=(const TimeStamp& other);
		TimeStamp operator+(const TimeStamp& other) const;
		TimeStamp operator-(const TimeStamp& other) const;

		/** @brief Returns a new time stamp holding the duration elapsed since this one */
		TimeStamp timeSince() const;

		/** @brief Returns the time elapsed since this time stamp, in seconds */
		float secondsSince() const;

		/** @brief Returns the time elapsed since this time stamp, in milliseconds */
		float millisecondsSince() const;

		/** @brief Returns the time elapsed since this time stamp, in microseconds */
		float microsecondsSince() const;

		/** @brief Returns the time elapsed since this time stamp, in nanoseconds */
		float nanosecondsSince() const;

		/** @brief Returns the raw counter value in ticks */
		inline std::uint64_t ticks() const {
			return _counter;
		}

		/** @brief Returns the stored counter value as seconds */
		float seconds() const;

		/** @brief Returns the stored counter value as milliseconds */
		float milliseconds() const;

		/** @brief Returns the stored counter value as microseconds */
		float microseconds() const;

		/** @brief Returns the stored counter value as nanoseconds */
		float nanoseconds() const;

	private:
		std::uint64_t _counter;

		explicit TimeStamp(std::uint64_t counter);
	};
}