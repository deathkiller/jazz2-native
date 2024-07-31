#pragma once

#include "RumbleDescription.h"

namespace Jazz2
{
	class JoyMapping;

	class RumbleProcessor
	{
	public:
		RumbleProcessor();
		~RumbleProcessor();

		void CancelAllEffects();
		void ExecuteEffect(std::int32_t joyId, const std::shared_ptr<RumbleDescription>& desc);
		void OnEndFrame(float timeMult);

	private:
		struct ActiveRumble
		{
			std::shared_ptr<RumbleDescription> Desc;
			std::int32_t JoyId;
			float Progress;

			ActiveRumble(std::int32_t joyId, std::shared_ptr<RumbleDescription> desc);
		};

		SmallVector<ActiveRumble, 0> _activeRumble;
	};
}
