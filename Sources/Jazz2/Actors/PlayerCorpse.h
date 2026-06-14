#pragma once

#include "ActorBase.h"

namespace Jazz2::Actors
{
	/**
		@brief Represents a dead corpse of a player
		
		The leftover body of a defeated player that remains in the level as decoration after the player dies.
	*/
	class PlayerCorpse : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Creates a new instance */
		PlayerCorpse();
		~PlayerCorpse();

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;

	private:
		// Allocated palette offset into the shared palette texture for the corpse recolor (-1 = none)
		std::int32_t _paletteOffset;
	};
}