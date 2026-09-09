#pragma once

#include "../../Main.h"

#include <algorithm>

#include <Containers/Function.h>

using namespace Death::Containers;

namespace Jazz2::Compatibility
{
	/**
		@brief Reports how far a conversion of the original game files has got
		
		The conversion is the only part of the first start that takes long enough to be worth showing --- on a slow
		device it isn't over by the time the intro video is, so the loading screen that follows it shows a progress
		bar instead of only the spinner.
		
		A step of the conversion is handed a reporter over the part of the whole that it is responsible for, so it
		doesn't have to know where in the whole that part sits --- it reports 0 to 1 of its own work and
		@ref Narrow() maps that into the enclosing range. The callback is not owned, which keeps a reporter cheap to
		copy and to pass by value, so whoever owns it has to keep it alive for as long as the conversion runs.
		
		Reported from whatever thread drives the conversion --- the parallel initialization thread in the game ---
		so the callback must not touch anything that the main thread owns.
	*/
	class ConversionProgress
	{
	public:
		/** @brief Creates a reporter that discards everything reported to it */
		ConversionProgress() noexcept
			: _callback(nullptr), _base(0.0f), _scale(1.0f) {}
		/** @brief Creates a reporter that reports the whole range to the specified callback */
		explicit ConversionProgress(Function<void(float)>* callback) noexcept
			: _callback(callback), _base(0.0f), _scale(1.0f) {}

		/**
		 * @brief Returns `true` if anything listens to the reported progress
		 *
		 * A measurement that is only needed to report progress --- counting the files to be converted ahead of
		 * time, for example --- can be skipped entirely otherwise.
		 */
		bool IsActive() const noexcept {
			return (_callback != nullptr && *_callback);
		}

		/** @brief Reports the progress of the current range, from 0 to 1 */
		void Report(float value) const {
			if (_callback == nullptr || !*_callback) {
				return;
			}

			(*_callback)(_base + std::min(std::max(value, 0.0f), 1.0f) * _scale);
		}

		/** @brief Reports the progress of the current range as @p current out of @p total steps */
		void ReportStep(std::int32_t current, std::int32_t total) const {
			if (total > 0) {
				Report((float)current / (float)total);
			}
		}

		/**
		 * @brief Returns a reporter over the specified sub-range of the current range
		 *
		 * Both bounds are fractions of the current range, so a step that takes up its last tenth is handed
		 * @cpp Narrow(0.9f, 1.0f) @ce however large the current range itself is.
		 */
		ConversionProgress Narrow(float from, float to) const noexcept {
			return ConversionProgress(_callback, _base + from * _scale, (to - from) * _scale);
		}

	private:
		ConversionProgress(Function<void(float)>* callback, float base, float scale) noexcept
			: _callback(callback), _base(base), _scale(scale) {}

		Function<void(float)>* _callback;
		float _base;
		float _scale;
	};
}
