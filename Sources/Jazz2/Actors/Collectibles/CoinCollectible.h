#pragma once

#include "CollectibleBase.h"

namespace Jazz2::Actors::Collectibles
{
	/** @brief Coin (collectible) */
	class CoinCollectible : public CollectibleBase
	{
		DEATH_RUNTIME_OBJECT(CollectibleBase);

	public:
		CoinCollectible();

		static void Preload(const ActorActivationDetails& details);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		std::int32_t _coinValue;
#endif

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnCollect(Player* player) override;
	};
}