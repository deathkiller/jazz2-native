#pragma once

#include "CollectibleBase.h"
#include "../../LevelInitialization.h"

namespace Jazz2::Actors::Collectibles
{
	class AmmoCollectible : public CollectibleBase
	{
	public:
		AmmoCollectible();

		static void Preload(const ActorActivationDetails& details);

	protected:
		WeaponType _weaponType;

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnCollect(Player* player) override;
	};
}