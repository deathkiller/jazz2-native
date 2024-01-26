#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	class FatChick : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		FatChick();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr float DefaultSpeed = 0.9f;

		bool _isAttacking;
		bool _stuck;

		void Attack();
	};
}