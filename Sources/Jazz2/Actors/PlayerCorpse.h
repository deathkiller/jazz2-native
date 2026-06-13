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
		~PlayerCorpse();

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;

	private:
		// Allocated palette offset into the shared palette texture for the corpse recolor (-1 = none)
		std::int32_t _paletteOffset;
	};
}