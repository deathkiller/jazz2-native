#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
	 * @brief Raven
	 *
	 * Black bird that hovers in place with a gentle bobbing motion and, when a player comes within
	 * range, swoops down to attack from above before gliding back to its origin, staying clear of the
	 * water. Takes a couple of hits to defeat.
	 */
	class Raven : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Raven();

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

		void AttackNearestPlayer();
	};
}