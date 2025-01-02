#pragma once

#include "RumbleDescription.h"

namespace Jazz2
{
	class JoyMapping;

	/** @brief Gamepad rumble processor */
	class RumbleProcessor
	{
	public:
		RumbleProcessor();
		~RumbleProcessor();

		void CancelAllEffects();
		void ExecuteEffect(std::int32_t joyId, const std::shared_ptr<RumbleDescription>& desc);
		void OnEndFrame(float timeMult);

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct ActiveRumble
		{
			std::shared_ptr<RumbleDescription> Desc;
			std::int32_t JoyId;
			float Progress;

			ActiveRumble(std::int32_t joyId, std::shared_ptr<RumbleDescription> desc);
		};
#endif

		SmallVector<ActiveRumble, 0> _activeRumble;
	};
}
