#pragma once

#include "ActorBase.h"

namespace Jazz2::Actors
{
	class PlayerCorpse : public ActorBase
	{
	public:
		PlayerCorpse();

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
	};
}