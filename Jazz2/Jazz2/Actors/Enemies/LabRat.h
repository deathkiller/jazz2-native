#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	class LabRat : public EnemyBase
	{
	public:
		LabRat();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr float DefaultSpeed = 1.0f;

		bool _isAttacking;
		bool _canAttack;
		bool _idling;
		bool _canIdle;
		float _stateTime;
		float _attackTime;

		void Idle(float timeMult);
		void Walking(float timeMult);
		void Attack();
	};
}