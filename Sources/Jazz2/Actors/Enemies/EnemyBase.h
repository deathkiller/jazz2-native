#pragma once

#include "../ActorBase.h"
#include "../../Direction.h"

namespace Jazz2::Actors::Enemies
{
	/** @brief Base class of an enemy */
	class EnemyBase : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		EnemyBase();

		bool CanCollideWithAmmo;

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		bool CanHurtPlayer() const
		{
			return (_canHurtPlayer && _frozenTimeLeft <= 0.0f);
		}

		bool IsFrozen() const
		{
			return (_frozenTimeLeft > 0.0f);
		}

	protected:
		bool _canHurtPlayer;
		uint32_t _scoreValue;
		Direction _lastHitDir;

		void OnUpdate(float timeMult) override;
		void OnHealthChanged(ActorBase* collider) override;
		bool OnPerish(ActorBase* collider) override;

		void AddScoreToCollider(ActorBase* collider);
		void SetHealthByDifficulty(int health);
		void PlaceOnGround();
		bool CanMoveToPosition(float x, float y);
		void TryGenerateRandomDrop();
		void CreateDeathDebris(ActorBase* collider);
		void StartBlinking();
		void HandleBlinking(float timeMult);

	private:
		float _blinkingTimeout;
	};
}