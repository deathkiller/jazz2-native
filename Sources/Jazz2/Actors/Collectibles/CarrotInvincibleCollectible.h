#pragma once

#include "CollectibleBase.h"

namespace Jazz2::Actors::Collectibles
{
	/**
	 * @brief Invincible carrot (collectible)
	 *
	 * The invincibility carrot from JJ2. Collecting it makes the player invulnerable to damage for a fixed
	 * duration and awards points.
	 */
	class CarrotInvincibleCollectible : public CollectibleBase
	{
		DEATH_RUNTIME_OBJECT(CollectibleBase);

	public:
		/** @brief Creates a new instance */
		CarrotInvincibleCollectible();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnCollect(Player* player) override;
	};
}