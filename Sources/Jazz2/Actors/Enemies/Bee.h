﻿#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/** @brief Bee */
	class Bee : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		Bee();
		~Bee();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		Vector2f _originPos, _lastPos, _targetPos, _lastSpeed;
		float _anglePhase;
		float _attackTime;
		bool _attacking, _returning;
		std::shared_ptr<AudioBufferPlayer> _noise;

		void AttackNearestPlayer();
	};
}