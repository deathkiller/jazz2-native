#pragma once

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace Jazz2
{
	class RumbleProcessor;

	class RumbleDescription
	{
		friend class RumbleProcessor;

	public:
		RumbleDescription() {}

		void AddToTimeline(float endTime, float lowFreq, float highFreq, float leftTrigger = 0.0f, float rightTrigger = 0.0f)
		{
			_timeline.emplace_back(endTime, lowFreq, highFreq, leftTrigger, rightTrigger);
		}

	private:
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

		SmallVector<TimelineItem, 0> _timeline;
	};
}
