#pragma once

#include "TimeStamp.h"

namespace nCine
{
	/**
		@brief Tracks per-frame timing and computes the average FPS and frame time multiplier
		
		Call @ref AddFrame() once per rendered frame to measure the interval since the previous one.
		The class derives the last frame duration, a periodically updated average FPS, and a time
		multiplier (@ref GetTimeMult()) expressing how long the last frame took relative to the
		nominal frame time, used to keep updates frame-rate independent.
	*/
	class FrameTimer
	{
	public:
		/** @brief Nominal frames per second */
		static constexpr float FramesPerSecond = 60.0f;
		/** @brief Nominal seconds per frame */
		static constexpr float SecondsPerFrame = 1.0f / FramesPerSecond;

		/**
		 * @brief Constructor
		 *
		 * @param logInterval	Seconds between two logging events
		 * @param avgInterval	Seconds between two average FPS calculations
		 */
		FrameTimer(float logInterval, float avgInterval);

		/** @brief Adds a frame to the counter and calculates the interval since the previous one */
		void AddFrame();

		/** @brief Starts counting the suspension time */
		void Suspend();
		/**
		 * @brief Drifts the timers by the duration of the last suspension and resumes counting
		 *
		 * @return Time stamp holding the duration of the last suspension
		 */
		TimeStamp Resume();

		/** @brief Returns the total number of frames counted */
		inline std::uint32_t GetTotalNumberFrames() const {
			return _totNumFrames;
		}
		/** @brief Returns the duration in seconds between the last two calls to @ref AddFrame() */
		inline float GetLastFrameDuration() const {
			return _frameDuration;
		}
		/** @brief Returns the duration in seconds since the last call to @ref AddFrame() */
		inline float GetFrameDuration() const {
			return _frameStart.secondsSince();
		}
		/** @brief Returns the duration in ticks since the last call to @ref AddFrame() */
		inline std::uint64_t GetFrameDurationAsTicks() const {
			return _frameStart.timeSince().ticks();
		}
		/** @brief Returns the average FPS measured over the averaging interval */
		inline float GetAverageFps() const {
			return _avgFps;
		}

		/** @brief Returns the factor expressing how long the last frame took relative to the nominal frame time */
		inline float GetTimeMult() const {
			return _timeMults[0];
		}

	private:
		// Number of seconds between two average FPS calculations (user defined)
		float _averageInterval;
		// Number of seconds between two logging events (user defined)
		float _loggingInterval;

		// Time stamp at the beginning of a frame
		TimeStamp _frameStart;
		// Last frame duration in seconds
		float _frameDuration;
		// Time stamp at the beginning of application suspension
		TimeStamp _suspensionStart;

		// Total number of frames counted
		std::uint32_t _totNumFrames;
		// Frame counter for average FPS calculation
		std::uint32_t _avgNumFrames;
		// Frame counter for logging
		std::uint32_t _logNumFrames;

		// Time stamp at last average FPS calculation
		TimeStamp _lastAvgUpdate;
		// Time stamp at last log event
		TimeStamp _lastLogUpdate;

		// Average FPS calculated during the averaging interval
		float _avgFps;

		// Factors expressing how long the last frames took relative to the nominal frame time
		float _timeMults[3];
	};
}