#pragma once

#include "CollectibleBase.h"

namespace Jazz2::Actors::Collectibles
{
	/**
		@brief Coin (collectible)
		
		Silver and gold coins from JJ2, worth one and five coins respectively. Coins accumulate in the
		player's coin total (separate from the gem/score total) and are spent at bonus warps to access
		secret areas. Awards points on pickup.
	*/
	class CoinCollectible : public CollectibleBase
	{
		DEATH_RUNTIME_OBJECT(CollectibleBase);

	public:
		/** @brief Creates a new instance */
		CoinCollectible();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		std::int32_t _coinValue;
#endif

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnCollect(Player* player) override;
	};
}