﻿#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/** @brief Raven */
	class Raven : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		Raven();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		Vector2f _originPos, _lastPos, _targetPos, _lastSpeed;
		float _anglePhase;
		float _attackTime;
		bool _attacking;

		void AttackNearestPlayer();
	};
}