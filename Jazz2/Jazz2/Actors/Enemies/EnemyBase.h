#pragma once

#include "../../ActorBase.h"

namespace Jazz2::Actors::Enemies
{
	class EnemyBase : public ActorBase
	{
	public:
		EnemyBase();

		bool CanCollideWithAmmo;

		bool OnHandleCollision(ActorBase* other) override;

		bool CanHurtPlayer()
		{
			return _canHurtPlayer && _frozenTimeLeft <= 0.0f;
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

		bool OnPerish(ActorBase* collider) override;

		void SetHealthByDifficulty(int health);
		bool CanMoveToPosition(float x, float y);
		void TryGenerateRandomDrop();
		void CreateDeathDebris(ActorBase* collider);

	private:
		float blinkingTimeout;
	};
}