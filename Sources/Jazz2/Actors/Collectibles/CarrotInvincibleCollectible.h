#pragma once

#include "CollectibleBase.h"

namespace Jazz2::Actors::Collectibles
{
	class CarrotInvincibleCollectible : public CollectibleBase
	{
		DEATH_RUNTIME_OBJECT(CollectibleBase);

	public:
		CarrotInvincibleCollectible();

		static void Preload(const ActorActivationDetails& details)
		{
			PreloadMetadataAsync("Collectible/CarrotInvincible"_s);
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnCollect(Player* player) override;
	};
}