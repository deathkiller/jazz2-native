#pragma once

#include "ActorBase.h"

namespace Jazz2::Actors
{
	/** @brief Represents a dead coprse of a player */
	class PlayerCorpse : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		PlayerCorpse();

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
	};
}