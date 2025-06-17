#pragma once

#include "CollectibleBase.h"
#include "../../LevelInitialization.h"

namespace Jazz2::Actors::Collectibles
{
	/** @brief Ammo collectible */
	class AmmoCollectible : public CollectibleBase
	{
		DEATH_RUNTIME_OBJECT(CollectibleBase);

	public:
		AmmoCollectible();

		static void Preload(const ActorActivationDetails& details);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		WeaponType _weaponType;
#endif

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnCollect(Player* player) override;
	};
}