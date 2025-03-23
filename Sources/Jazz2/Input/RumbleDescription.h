#pragma once

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace Jazz2::Input
{
	class RumbleProcessor;

	/** @brief Describes a gamepad rumble effect */
	class RumbleDescription
	{
		friend class RumbleProcessor;

	public:
		RumbleDescription() {}

		/** @brief Adds a new frame to the effect timeline */
		void AddToTimeline(float endTime, float lowFreq, float highFreq, float leftTrigger = 0.0f, float rightTrigger = 0.0f) {
			_timeline.emplace_back(endTime, lowFreq, highFreq, leftTrigger, rightTrigger);
		}

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct TimelineItem {
			float EndTime;

			float LowFrequency;
			float HighFrequency;
			float LeftTrigger;
			float RightTrigger;

			TimelineItem(float endTime, float lowFreq, float highFreq, float leftTrigger, float rightTrigger)
				: EndTime(endTime), LowFrequency(lowFreq), HighFrequency(highFreq), LeftTrigger(leftTrigger), RightTrigger(rightTrigger)
			{
			}
		};
#endif

		SmallVector<TimelineItem, 0> _timeline;
	};
}
