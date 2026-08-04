#pragma once

#include "../Primitives/Rect.h"

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	/**
		@brief Sequence of texture rectangles forming a sprite animation
		
		Stores an ordered list of source rectangles and their per-frame durations, advancing the current
		frame over time according to the configured @ref LoopMode. A sprite samples the @ref rect() of its
		animation each frame to display the matching region of its texture.
	*/
	class RectAnimation
	{
	public:
		/** @brief Animation loop modes */
		enum class LoopMode
		{
			NoRepeat,	/**< When the animation reaches the last frame it stops and pauses */
			FromStart,	/**< When the animation reaches the last frame it begins again from start */
			Backward	/**< When the animation reaches the last frame it plays in reverse, ping-ponging */
		};

		/** @brief Default constructor */
		RectAnimation()
			: RectAnimation(1.0f / 60.0f, LoopMode::NoRepeat) {}
		/** @brief Constructs an animation with the specified default frame duration and loop mode */
		RectAnimation(float defaultFrameDuration, LoopMode loopMode);

		/** @brief Returns whether the animation is paused */
		inline bool isPaused() const {
			return _isPaused;
		}
		/** @brief Sets the pause flag */
		inline void setPaused(bool isPaused) {
			_isPaused = isPaused;
		}

		/** @brief Advances the current frame by the time elapsed since the last update */
		void updateFrame(float interval);

		/** @brief Returns the current frame index */
		inline std::uint32_t frame() const {
			return _currentFrame;
		}
		/** @brief Sets the current frame index */
		void setFrame(std::uint32_t frameNum);

		/** @brief Returns the default frame duration in seconds */
		float defaultFrameDuration() const {
			return _defaultFrameDuration;
		}
		/** @brief Sets the default frame duration in seconds */
		inline void setDefaultFrameDuration(float defaultFrameDuration) {
			_defaultFrameDuration = defaultFrameDuration;
		}

		/** @brief Returns the loop mode */
		LoopMode loopMode() const {
			return _loopMode;
		}
		/** @brief Sets the loop mode */
		inline void setLoopMode(LoopMode loopMode) {
			_loopMode = loopMode;
		}

		/** @brief Appends a rectangle with the specified frame duration */
		void addRect(const Recti& rect, float frameTime);
		/** @brief Creates a rectangle from origin and size and appends it with the specified frame duration */
		void addRect(std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h, float frameTime);

		/** @brief Appends a rectangle with the default frame duration */
		inline void addRect(const Recti& rect) {
			addRect(rect, _defaultFrameDuration);
		}
		/** @brief Creates a rectangle from origin and size and appends it with the default frame duration */
		inline void addRect(std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h) {
			addRect(x, y, w, h, _defaultFrameDuration);
		}

		/** @brief Returns the current rectangle */
		inline const Recti& rect() const {
			return _rects[_currentFrame];
		}
		/** @brief Returns the current frame duration in seconds */
		inline float frameDuration() const {
			return _frameDurations[_currentFrame];
		}

		/** @brief Returns the array of all rectangles */
		inline SmallVectorImpl<Recti>& rectangles() {
			return _rects;
		}
		/** @brief Returns the array of all rectangles (read-only) */
		inline const SmallVectorImpl<Recti>& rectangles() const {
			return _rects;
		}

		/** @brief Returns the array of all frame durations */
		inline SmallVectorImpl<float>& frameDurations() {
			return _frameDurations;
		}
		/** @brief Returns the array of all frame durations (read-only) */
		inline const SmallVectorImpl<float>& frameDurations() const {
			return _frameDurations;
		}

	private:
		/** @brief Default frame duration used when a custom one is not specified when adding a rectangle */
		float _defaultFrameDuration;
		/** @brief Looping mode */
		LoopMode _loopMode;

		/** @brief Array of frame rectangles */
		SmallVector<Recti, 0> _rects;
		/** @brief Array of per-frame durations */
		SmallVector<float, 0> _frameDurations;
		/** @brief Current frame index */
		std::uint32_t _currentFrame;
		/** @brief Elapsed time since the last frame change */
		float _elapsedFrameTime;
		/** @brief Whether the animation is currently advancing forward (for @ref LoopMode::Backward) */
		bool _goingForward;
		/** @brief Pause flag */
		bool _isPaused;
	};

}
