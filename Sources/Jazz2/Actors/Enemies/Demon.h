#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	class Demon : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		Demon();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		float _attackTime;
		bool _attacking;
		bool _stuck;
		float _turnCooldown;
	};
}