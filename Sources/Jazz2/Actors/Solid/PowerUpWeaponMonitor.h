#pragma once

#include "../SolidObjectBase.h"

namespace Jazz2::Actors::Solid
{
	class PowerUpWeaponMonitor : public SolidObjectBase
	{
		DEATH_RUNTIME_OBJECT(SolidObjectBase);

	public:
		PowerUpWeaponMonitor();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		static void Preload(const ActorActivationDetails& details);

		void DestroyAndApplyToPlayer(Player* player);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		WeaponType _weaponType;
	};
}