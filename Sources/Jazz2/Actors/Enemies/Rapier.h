#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Rapier
		
		Ghostly sword-bearing enemy that floats with a gentle weaving motion, moaning occasionally, and
		lunges at the nearest player within range before drifting back to its origin. Takes a couple of
		hits and dissolves when defeated.
	*/
	class Rapier : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Rapier();

		/** @brief Preloads all assets required by this actor */
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
		float _noiseCooldown;

		void AttackNearestPlayer();
	};
}