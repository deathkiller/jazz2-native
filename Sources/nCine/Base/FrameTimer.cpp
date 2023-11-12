#include "FrameTimer.h"
#include "../../Common.h"

#include <algorithm>

namespace nCine
{
	FrameTimer::FrameTimer(float logInterval, float avgInterval)
		: averageInterval_(avgInterval), loggingInterval_(logInterval), frameDuration_(0.0f), lastAvgUpdate_(TimeStamp::now()),
			totNumFrames_(0L), avgNumFrames_(0L), logNumFrames_(0L), avgFps_(0.0f), timeMult_(1.0f), timeMultPrev_(1.0f)
	{
	}

	void FrameTimer::addFrame()
	{
		frameDuration_ = frameStart_.secondsSince();

		// Start counting for the next frame interval
		frameStart_ = TimeStamp::now();

		totNumFrames_++;
		avgNumFrames_++;
		logNumFrames_++;

		// Smooth out time multiplier using last 2 frames to prevent microstuttering
		float timeMultPrev = timeMult_;
		timeMult_ = (timeMultPrev_ + timeMultPrev_ + timeMult_ + (std::min(frameDuration_, SecondsPerFrame * 2) / SecondsPerFrame)) * 0.25f;
		timeMultPrev_ = timeMultPrev;

		// Update the FPS average calculation every `avgInterval_` seconds
		const float secsSinceLastAvgUpdate = (frameStart_ - lastAvgUpdate_).seconds();
		if (averageInterval_ > 0.0f && secsSinceLastAvgUpdate > averageInterval_) {
			avgFps_ = static_cast<float>(avgNumFrames_) / secsSinceLastAvgUpdate;

			avgNumFrames_ = 0L;
			lastAvgUpdate_ = frameStart_;
		}

		const float secsSinceLastLogUpdate = (frameStart_ - lastLogUpdate_).seconds();
		// Log number of frames and FPS every `logInterval_` seconds
		if (loggingInterval_ > 0.0f && avgNumFrames_ != 0 && secsSinceLastLogUpdate > loggingInterval_) {
			avgFps_ = static_cast<float>(logNumFrames_) / loggingInterval_;
#if defined(DEATH_TRACE) && defined(DEATH_DEBUG)
			const float msPerFrame = (loggingInterval_ * 1000.0f) / static_cast<float>(logNumFrames_);
			LOGD("%lu frames in %.0f seconds = %.1f FPS (%.2fms per frame)", logNumFrames_, loggingInterval_, avgFps_, msPerFrame);
#endif
			logNumFrames_ = 0L;
			lastLogUpdate_ = frameStart_;
		}
	}

	void FrameTimer::suspend()
	{
		suspensionStart_ = TimeStamp::now();
	}

	TimeStamp FrameTimer::resume()
	{
		const TimeStamp suspensionDuration = suspensionStart_.timeSince();
		frameStart_ += suspensionDuration;
		lastAvgUpdate_ += suspensionDuration;
		lastLogUpdate_ += suspensionDuration;

		return suspensionDuration;
	}
}
