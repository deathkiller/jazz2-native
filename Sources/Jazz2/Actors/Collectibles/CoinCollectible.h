#pragma once

#include "CollectibleBase.h"

namespace Jazz2::Actors::Collectibles
{
	class CoinCollectible : public CollectibleBase
	{
		DEATH_RUNTIME_OBJECT(CollectibleBase);

	public:
		CoinCollectible();

		static void Preload(const ActorActivationDetails& details)
		{
			PreloadMetadataAsync("Collectible/Coins"_s);
		}

	protected:
		int _coinValue;

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnCollect(Player* player) override;
	};
}