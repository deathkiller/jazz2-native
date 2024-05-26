#pragma once

#include "TimeStamp.h"

namespace nCine
{
	/// Frame interval and average FPS calculator class
	class FrameTimer
	{
	public:
		static constexpr float FramesPerSecond = 60.0f;
		static constexpr float SecondsPerFrame = 1.0f / FramesPerSecond;

		/// Constructor
		FrameTimer(float logInterval, float avgInterval);

		/// Adds a frame to the counter and calculates the interval since the previous one
		void AddFrame();

		/// Starts counting the suspension time
		void Suspend();
		/// Drifts timers by the duration of last suspension
		/*! \return A timestamp with last suspension duration */
		TimeStamp Resume();

		/// Returns the total number of frames counted
		inline std::uint32_t GetTotalNumberFrames() const {
			return _totNumFrames;
		}
		/// Returns the last frame duration in seconds between the last two subsequent calls to `addFrame()`
		inline float GetLastFrameDuration() const {
			return _frameDuration;
		}
		/// Returns current frame duration in seconds since the last call to `addFrame()`
		inline float GetFrameDuration() const {
			return _frameStart.secondsSince();
		}
		/// Returns current frame duration in ticks since the last call to `addFrame()`
		inline std::uint64_t GetFrameDurationAsTicks() const {
			return _frameStart.timeSince().ticks();
		}
		/// Returns the average FPS during the update interval
		inline float GetAverageFps() const {
			return _avgFps;
		}

		/// Returns a factor that represents how long the last frame took relative to the desired frame time
		inline float GetTimeMult() const {
			return _timeMults[0];
		}

	private:
		/// Number of seconds between two average FPS calculations (user defined)
		float _averageInterval;
		/// Number of seconds between two logging events (user defined)
		float _loggingInterval;

		/// Timestamp at the beginning of a frame
		TimeStamp _frameStart;
		/// Last frame duration in seconds
		float _frameDuration;
		/// Timestamp at the begininng of application suspension
		TimeStamp _suspensionStart;

		/// Total number of frames counted
		std::uint32_t _totNumFrames;
		/// Frame counter for average FPS calculation
		std::uint32_t _avgNumFrames;
		/// Frame counter for logging
		std::uint32_t _logNumFrames;

		/// Time stamp at last average FPS calculation
		TimeStamp _lastAvgUpdate;
		/// Time stamp at last log event
		TimeStamp _lastLogUpdate;

		/// Average FPS calulated during the specified interval
		float _avgFps;

		/// Factor that represents how long the last frame took relative to the desired frame time
		float _timeMults[3];
	};
}