#pragma once

#include "CollectibleBase.h"

namespace Jazz2::Actors::Collectibles
{
	/**
	 * @brief Fly carrot (collectible)
	 *
	 * The flying carrot (copter ears) power-up from JJ2. Collecting it lets the player hover and fly for a
	 * limited time by applying the copter modifier, and awards points.
	 */
	class CarrotFlyCollectible : public CollectibleBase
	{
		DEATH_RUNTIME_OBJECT(CollectibleBase);

	public:
		/** @brief Creates a new instance */
		CarrotFlyCollectible();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnCollect(Player* player) override;
	};
}