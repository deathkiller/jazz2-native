#include "FrameTimer.h"
#include "../../Main.h"

#include <algorithm>

namespace nCine
{
#if defined(WITH_LIBRETRO)
	float FrameTimer::FixedFrameDuration = FrameTimer::SecondsPerFrame;
#endif

	FrameTimer::FrameTimer(float logInterval, float avgInterval)
		: _averageInterval(avgInterval), _loggingInterval(logInterval), _frameDuration(0.0f), _lastAvgUpdate(TimeStamp::now()),
			_totNumFrames(0L), _avgNumFrames(0L), _logNumFrames(0L), _avgFps(0.0f), _timeMults{1.0f, 1.0f, 1.0f}
	{
	}

	void FrameTimer::AddFrame()
	{
		_frameDuration = _frameStart.secondsSince();
#if defined(WITH_LIBRETRO)
		// libretro: the frontend paces retro_run at exactly one frame of its announced rate, so use
		// a fixed timestep — wall-clock jitter would otherwise leak into game speed and cause
		// scrolling judder. The game speed stays correct at any rate through GetTimeMult().
		_frameDuration = FixedFrameDuration;
#endif

		// Start counting for the next frame interval
		_frameStart = TimeStamp::now();

		_totNumFrames++;
		_avgNumFrames++;
		_logNumFrames++;

		// Smooth out time multiplier using last 3 frames to prevent microstuttering
		const float timeMultLast = _timeMults[0];
		_timeMults[0] = (_timeMults[2] + _timeMults[1] + timeMultLast + (std::min(_frameDuration, SecondsPerFrame * 2) / SecondsPerFrame)) * 0.25f;
		_timeMults[2] = _timeMults[1];
		_timeMults[1] = timeMultLast;

		// Update the FPS average calculation every `avgInterval_` seconds
		if (_frameStart < _lastAvgUpdate || _frameStart < _lastLogUpdate) {
			LOGW("Detected time discontinuity, resetting counters");
			_lastAvgUpdate = _frameStart;
			_lastLogUpdate = _frameStart;
		}

		const float secsSinceLastAvgUpdate = (_frameStart - _lastAvgUpdate).seconds();
		if (_averageInterval > 0.0f && secsSinceLastAvgUpdate > _averageInterval) {
			_avgFps = static_cast<float>(_avgNumFrames) / secsSinceLastAvgUpdate;

			_avgNumFrames = 0L;
			_lastAvgUpdate = _frameStart;
		}

		const float secsSinceLastLogUpdate = (_frameStart - _lastLogUpdate).seconds();
		// Log number of frames and FPS every `logInterval_` seconds
		if (_loggingInterval > 0.0f && _avgNumFrames != 0 && secsSinceLastLogUpdate > _loggingInterval) {
			_avgFps = static_cast<float>(_logNumFrames) / _loggingInterval;
#if defined(DEATH_TRACE) && defined(DEATH_DEBUG)
			//const float msPerFrame = (_loggingInterval * 1000.0f) / static_cast<float>(_logNumFrames);
			//LOGD("{} frames in {:.0} seconds = {:.1} FPS ({:.2}ms per frame)", _logNumFrames, _loggingInterval, _avgFps, msPerFrame);
#endif
			_logNumFrames = 0L;
			_lastLogUpdate = _frameStart;
		}
	}

	void FrameTimer::Suspend()
	{
		_suspensionStart = TimeStamp::now();
	}

	TimeStamp FrameTimer::Resume()
	{
		const TimeStamp suspensionDuration = _suspensionStart.timeSince();
		_frameStart += suspensionDuration;
		_lastAvgUpdate += suspensionDuration;
		_lastLogUpdate += suspensionDuration;

		return suspensionDuration;
	}
}
