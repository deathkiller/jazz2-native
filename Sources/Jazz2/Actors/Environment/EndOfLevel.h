#pragma once

#include "../ActorBase.h"
#include "../../ExitType.h"

namespace Jazz2::Actors::Environment
{
	/** @brief End of level sign */
	class EndOfLevel : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		EndOfLevel();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdateHitbox() override;

	private:
		ExitType _exitType;
	};
}