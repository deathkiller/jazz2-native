#pragma once

#include "../SolidObjectBase.h"
#include "../../ShieldType.h"

namespace Jazz2::Actors::Solid
{
	class PowerUpShieldMonitor : public SolidObjectBase
	{
	public:
		PowerUpShieldMonitor();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		static void Preload(const ActorActivationDetails& details);

		void DestroyAndApplyToPlayer(Player* player);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		ShieldType _shieldType;
	};
}