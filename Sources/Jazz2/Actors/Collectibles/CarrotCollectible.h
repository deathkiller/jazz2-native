#pragma once

#include "CollectibleBase.h"

namespace Jazz2::Actors::Collectibles
{
	/**
	 * @brief Carrot (collectible)
	 *
	 * The classic Jazz Jackrabbit health pickup. A regular carrot restores a single unit of health, while
	 * the full (max) carrot heals the player to full health and grants brief invulnerability. Awards points
	 * on pickup.
	 */
	class CarrotCollectible : public CollectibleBase
	{
		DEATH_RUNTIME_OBJECT(CollectibleBase);

	public:
		/** @brief Creates a new instance */
		CarrotCollectible();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		bool _maxCarrot;
#endif

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnCollect(Player* player) override;
	};
}