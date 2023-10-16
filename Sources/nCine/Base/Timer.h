#pragma once

#include <cstdint>

namespace nCine
{
	/// Basic timer and synchronization class
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
			accumulatedTime_ = 0ULL;
		}
		/// Returns `true` if the timer is running
		inline bool isRunning() const {
			return isRunning_;
		}

		/// Returns elapsed time in seconds since last starting the timer
		float interval() const;
		/// Returns total accumulated time in seconds
		float total() const;

		/// Puts the current thread to sleep for the specified number of milliseconds
		static void sleep(std::uint32_t milliseconds);

	private:
		bool isRunning_;
		/// Start time mark
		uint64_t startTime_;
		/// Accumulated time ticks over multiple start and stop
		uint64_t accumulatedTime_;
	};
}