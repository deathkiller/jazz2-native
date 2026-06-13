#pragma once

#include "CollectibleBase.h"

namespace Jazz2::Actors::Collectibles
{
	/** @brief Fast fire (collectible) */
	class FastFireCollectible : public CollectibleBase
	{
		DEATH_RUNTIME_OBJECT(CollectibleBase);

	public:
		FastFireCollectible();
		~FastFireCollectible();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnCollect(Player* player) override;

	private:
		// Allocated palette offset into the shared palette texture for recoloring to match the player (-1 = none)
		std::int32_t _paletteOffset;
	};
}