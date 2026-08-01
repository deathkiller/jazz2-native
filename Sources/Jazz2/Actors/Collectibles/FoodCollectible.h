#pragma once

#include "CollectibleBase.h"
#include "FoodType.h"

namespace Jazz2::Actors::Collectibles
{
	/**
		@brief Food (collectible)
		
		The assorted food and drink items scattered throughout JJ2 levels (fruit, sweets, fast food, sodas,
		etc.). Each item awards a small amount of points and counts toward the player's food tally; eating
		enough food triggers the Sugar Rush power-up.
	*/
	class FoodCollectible : public CollectibleBase
	{
		DEATH_RUNTIME_OBJECT(CollectibleBase);

	public:
		/** @brief Creates a new instance */
		FoodCollectible();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		bool _isDrinkable;
#endif

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnCollect(Player* player) override;
	};
}