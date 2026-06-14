#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	/**
		@brief Ice block
		
		Block of ice that blocks the player's path and shatters after a short time, scattering ice
		shrapnel. It disappears early if the tile it occupies is cleared, and its timer can be
		refreshed to keep it solid.
	*/
	class IceBlock : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Creates a new instance */
		IceBlock();

		/** @brief Resets the remaining time before the ice block disappears */
		void ResetTimeLeft();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;

	private:
		float _timeLeft;
	};
}