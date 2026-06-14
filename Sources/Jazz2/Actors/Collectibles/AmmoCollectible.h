#pragma once

#include "CollectibleBase.h"
#include "../../LevelInitialization.h"

namespace Jazz2::Actors::Collectibles
{
	/**
	 * @brief Ammo (collectible)
	 *
	 * Weapon ammunition pickup found throughout levels and dropped by enemies. Collecting it replenishes
	 * the player's stock of the matching weapon (Bouncer, Freezer, Seeker, RF, Toaster, TNT, Pepper, etc.)
	 * and awards points. Crates and barrels in JJ2 break open to scatter several of these.
	 */
	class AmmoCollectible : public CollectibleBase
	{
		DEATH_RUNTIME_OBJECT(CollectibleBase);

	public:
		/** @brief Creates a new instance */
		AmmoCollectible();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		WeaponType _weaponType;
#endif

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnCollect(Player* player) override;
	};
}