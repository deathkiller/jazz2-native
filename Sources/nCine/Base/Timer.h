#pragma once

#include <cstdint>

namespace nCine
{
	/// Accumulates the time between starting and stopping the time measurement
	class Timer
	{
	public:
		Timer();

		/// Starts the timer
		void start();
		/// Stops the timer
		void stop();
		/// Resets the accumulated time
		inline void reset() {
			_accumulatedTime = 0ULL;
		}
		/// Returns `true` if the timer is running
		inline bool isRunning() const {
			return _isRunning;
		}

		/// Returns elapsed time in seconds since last starting the timer
		float interval() const;
		/// Returns total accumulated time in seconds
		float total() const;

	private:
		bool _isRunning;
		/// Start time mark
		std::uint64_t _startTime;
		/// Accumulated time ticks over multiple start and stop
		std::uint64_t _accumulatedTime;
	};
}