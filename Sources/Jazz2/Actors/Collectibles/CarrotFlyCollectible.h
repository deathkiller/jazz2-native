#pragma once

#include "CollectibleBase.h"

namespace Jazz2::Actors::Collectibles
{
	/** @brief Fly carrot collectible */
	class CarrotFlyCollectible : public CollectibleBase
	{
		DEATH_RUNTIME_OBJECT(CollectibleBase);

	public:
		CarrotFlyCollectible();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnCollect(Player* player) override;
	};
}