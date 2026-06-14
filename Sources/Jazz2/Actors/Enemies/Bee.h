#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Bee
		
		Floating insect that bobs in a small circular pattern near its origin and periodically darts out
		to lunge at the nearest player within range, buzzing as it attacks, before drifting back to its
		starting point. Defeated by a single hit.
	*/
	class Bee : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Bee();
		~Bee();

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
		bool _attacking, _returning;
		std::shared_ptr<AudioBufferPlayer> _noise;

		void AttackNearestPlayer();
	};
}