#include "RectAnimation.h"
#include "../../Main.h"

namespace nCine
{
	RectAnimation::RectAnimation(float defaultFrameDuration, LoopMode loopMode)
		: defaultFrameDuration_(defaultFrameDuration), loopMode_(loopMode), rects_(4), frameDurations_(4),
			currentFrame_(0), elapsedFrameTime_(0.0f), goingForward_(true), isPaused_(true)
	{
	}

	void RectAnimation::updateFrame(float timeMult)
	{
		// No frame calculation if the animation is paused or has only one rect
		if (isPaused_ || rects_.size() < 2) {
			return;
		}

		elapsedFrameTime_ += timeMult;
		// Determine the next frame rectangle
		while (elapsedFrameTime_ >= frameDurations_[currentFrame_]) {
			elapsedFrameTime_ -= frameDurations_[currentFrame_];

			if (goingForward_) {
				if (currentFrame_ == rects_.size() - 1) {
					if (loopMode_ == LoopMode::Backward) {
						goingForward_ = false;
						currentFrame_--;
					} else {
						if (loopMode_ == LoopMode::NoRepeat) {
							isPaused_ = true;
						} else {
							currentFrame_ = 0;
						}
					}
				} else {
					currentFrame_++;
				}
			} else {
				if (currentFrame_ == 0) {
					if (loopMode_ == LoopMode::NoRepeat) {
						isPaused_ = true;
					} else {
						goingForward_ = true;
						currentFrame_++;
					}
				} else {
					currentFrame_--;
				}
			}
		}
	}

	void RectAnimation::setFrame(std::uint32_t frameNum)
	{
		ASSERT(frameNum < rects_.size());
		currentFrame_ = frameNum;
	}

	void RectAnimation::addRect(const Recti& rect, float frameDuration)
	{
		rects_.push_back(rect);
		frameDurations_.push_back(frameDuration);
	}

	void RectAnimation::addRect(std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h, float frameDuration)
	{
		rects_.push_back(Recti(x, y, w, h));
		frameDurations_.push_back(frameDuration);
	}
}
