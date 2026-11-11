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
		bool _isGold;
		bool _transmuted;
#endif

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		void OnCollect(Player* player) override;

	private:
		// Turns the coin into the gem a bonus warp makes of it
		void Transmute();

		std::int32_t GetCoinValue() const {
			return (_transmuted ? 0 : (_isGold ? 5 : 1));
		}
	};
}