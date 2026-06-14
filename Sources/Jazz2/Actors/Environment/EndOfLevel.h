#pragma once

#include "../ActorBase.h"
#include "../../ExitType.h"

namespace Jazz2::Actors::Environment
{
	/**
		@brief End of level sign
		
		The level-exit sign post. When the player reaches and touches it, the level is completed
		and the game proceeds to the next stage according to the configured exit type.
	*/
	class EndOfLevel : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Creates a new instance */
		EndOfLevel();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdateHitbox() override;

	private:
		ExitType _exitType;
	};
}