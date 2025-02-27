#pragma once

#include "RumbleDescription.h"

namespace Jazz2::Input
{
	/** @brief Gamepad rumble processor */
	class RumbleProcessor
	{
	public:
		RumbleProcessor();
		~RumbleProcessor();

		/** @brief Cancels all active effects */
		void CancelAllEffects();
		/** @brief Executes an effect on a given gamepad */
		void ExecuteEffect(std::int32_t joyId, const std::shared_ptr<RumbleDescription>& desc);

		/** @brief Called at the end of each frame */
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
