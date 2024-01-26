#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Enemies
{
	class EnemyBase : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		EnemyBase();

		bool CanCollideWithAmmo;

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		bool CanHurtPlayer()
		{
			return (_canHurtPlayer && _frozenTimeLeft <= 0.0f);
		}

	protected:
		enum class LastHitDirection {
			None,
			Left,
			Right,
			Up,
			Down
		};

		bool _canHurtPlayer;
		uint32_t _scoreValue;
		LastHitDirection _lastHitDir;

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