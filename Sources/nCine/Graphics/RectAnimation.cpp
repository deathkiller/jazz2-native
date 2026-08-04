#include "RectAnimation.h"
#include "../../Main.h"

namespace nCine
{
	RectAnimation::RectAnimation(float defaultFrameDuration, LoopMode loopMode)
		: _defaultFrameDuration(defaultFrameDuration), _loopMode(loopMode), _rects(4), _frameDurations(4),
			_currentFrame(0), _elapsedFrameTime(0.0f), _goingForward(true), _isPaused(true)
	{
	}

	void RectAnimation::updateFrame(float timeMult)
	{
		// No frame calculation if the animation is paused or has only one rect
		if (_isPaused || _rects.size() < 2) {
			return;
		}

		_elapsedFrameTime += timeMult;
		// Determine the next frame rectangle
		while (_elapsedFrameTime >= _frameDurations[_currentFrame]) {
			_elapsedFrameTime -= _frameDurations[_currentFrame];

			if (_goingForward) {
				if (_currentFrame == _rects.size() - 1) {
					if (_loopMode == LoopMode::Backward) {
						_goingForward = false;
						_currentFrame--;
					} else {
						if (_loopMode == LoopMode::NoRepeat) {
							_isPaused = true;
						} else {
							_currentFrame = 0;
						}
					}
				} else {
					_currentFrame++;
				}
			} else {
				if (_currentFrame == 0) {
					if (_loopMode == LoopMode::NoRepeat) {
						_isPaused = true;
					} else {
						_goingForward = true;
						_currentFrame++;
					}
				} else {
					_currentFrame--;
				}
			}
		}
	}

	void RectAnimation::setFrame(std::uint32_t frameNum)
	{
		DEATH_ASSERT(frameNum < _rects.size());
		_currentFrame = frameNum;
	}

	void RectAnimation::addRect(const Recti& rect, float frameDuration)
	{
		_rects.push_back(rect);
		_frameDurations.push_back(frameDuration);
	}

	void RectAnimation::addRect(std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h, float frameDuration)
	{
		_rects.push_back(Recti(x, y, w, h));
		_frameDurations.push_back(frameDuration);
	}
}
