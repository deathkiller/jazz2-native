#pragma once

#include "../Primitives/Rect.h"

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	/// Contains data for a rectangles based animation
	class RectAnimation
	{
	public:
		/// Loop modes
		enum class LoopMode
		{
			NoRepeat,
			/// When the animation reaches the last frame it begins again from start
			FromStart,
			/// When the animation reaches the last frame it goes backward
			Backward
		};

		/// Default constructor
		RectAnimation()
			: RectAnimation(1.0f / 60.0f, LoopMode::NoRepeat) {}
		/// Constructor for an animation with a specified default frame duration, loop and rewind mode
		RectAnimation(float defaultFrameDuration, LoopMode loopMode);

		/// Returns `true` if the animation is paused
		inline bool isPaused() const {
			return isPaused_;
		}
		/// Sets the pause flag
		inline void setPaused(bool isPaused) {
			isPaused_ = isPaused;
		}

		/// Updates current frame based on time passed
		void updateFrame(float interval);

		/// Returns current frame
		inline std::uint32_t frame() const {
			return currentFrame_;
		}
		/// Sets current frame
		void setFrame(std::uint32_t frameNum);

		/// Returns the default frame duration in seconds
		float defaultFrameDuration() const {
			return defaultFrameDuration_;
		}
		/// Sets the default frame duration in seconds
		inline void setDefaultFrameDuration(float defaultFrameDuration) {
			defaultFrameDuration_ = defaultFrameDuration;
		}

		/// Returns the loop mode
		LoopMode loopMode() const {
			return loopMode_;
		}
		/// Sets the loop mode
		inline void setLoopMode(LoopMode loopMode) {
			loopMode_ = loopMode;
		}

		/// Adds a rectangle to the array specifying the frame duration
		void addRect(const Recti& rect, float frameTime);
		/// Creates a rectangle from origin and size and then adds it to the array, specifying the frame duration
		void addRect(std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h, float frameTime);

		/// Adds a rectangle to the array with the default frame duration
		inline void addRect(const Recti& rect) {
			addRect(rect, defaultFrameDuration_);
		}
		/// Creates a rectangle from origin and size and then adds it to the array, with the default frame duration
		inline void addRect(std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h) {
			addRect(x, y, w, h, defaultFrameDuration_);
		}

		/// Returns the current rectangle
		inline const Recti& rect() const {
			return rects_[currentFrame_];
		}
		/// Returns the current frame duration in seconds
		inline float frameDuration() const {
			return frameDurations_[currentFrame_];
		}

		/// Returns the array of all rectangles
		inline SmallVectorImpl<Recti>& rectangles() {
			return rects_;
		}
		/// Returns the constant array of all rectangles
		inline const SmallVectorImpl<Recti>& rectangles() const {
			return rects_;
		}

		/// Returns the array of all frame durations
		inline SmallVectorImpl<float>& frameDurations() {
			return frameDurations_;
		}
		/// Returns the constant array of all frame durations
		inline const SmallVectorImpl<float>& frameDurations() const {
			return frameDurations_;
		}

	private:
		/// The default frame duratin if a custom one is not specified when adding a rectangle
		float defaultFrameDuration_;
		/// The looping mode
		LoopMode loopMode_;

		/// The rectangles array
		SmallVector<Recti, 0> rects_;
		/// The frame durations array
		SmallVector<float, 0> frameDurations_;
		/// Current frame
		std::uint32_t currentFrame_;
		/// Elapsed time since the last frame change
		float elapsedFrameTime_;
		/// The flag about the frame advance direction
		bool goingForward_;
		/// The pause flag
		bool isPaused_;
	};

}
