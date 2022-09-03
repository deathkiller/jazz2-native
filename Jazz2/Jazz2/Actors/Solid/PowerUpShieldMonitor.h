#pragma once

#include "../SolidObjectBase.h"
#include "../../LevelInitialization.h"

namespace Jazz2::Actors::Solid
{
	class PowerUpShieldMonitor : public SolidObjectBase
	{
	public:
		PowerUpShieldMonitor();

		static void Preload(const ActorActivationDetails& details);

		void DestroyAndApplyToPlayer(Player* player);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		ShieldType _shieldType;
	};
}