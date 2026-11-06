#pragma once

#include <cstdint>

namespace nCine
{
	/**
		@brief Stopwatch that accumulates elapsed time across start/stop intervals
		
		Each @ref start()/@ref stop() pair adds its duration to a running total that can be queried
		with @ref total(). @ref interval() reports the time since the most recent @ref start().
	*/
	class Timer
	{
	public:
		Timer();

		/** @brief Starts the timer */
		void Start();
		/** @brief Stops the timer and adds the elapsed interval to the accumulated time */
		void Stop();
		/** @brief Resets the accumulated time to zero */
		inline void Reset() {
			_accumulatedTime = 0ULL;
		}
		/** @brief Returns `true` if the timer is currently running */
		inline bool IsRunning() const {
			return _isRunning;
		}

		/** @brief Returns the time in seconds elapsed since the last call to @ref start() */
		float GetInterval() const;
		/** @brief Returns the total accumulated time in seconds across all intervals */
		float GetTotal() const;

	private:
		bool _isRunning;
		// Start time mark
		std::uint64_t _startTime;
		// Accumulated time ticks over multiple start and stop
		std::uint64_t _accumulatedTime;
	};
}