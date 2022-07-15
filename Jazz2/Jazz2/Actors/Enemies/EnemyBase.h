#pragma once

#include "../../ActorBase.h"

namespace Jazz2::Actors::Enemies
{
	class EnemyBase : public ActorBase
	{
	public:
		EnemyBase();

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

		uint32_t _scoreValue;
		bool _canHurtPlayer;
		bool _canCollideWithAmmo;
		LastHitDirection _lastHitDir;

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnPerish(ActorBase* collider) override;

		void SetHealthByDifficulty(int health);
		bool CanMoveToPosition(float x, float y);
		void TryGenerateRandomDrop();

	private:
		float blinkingTimeout;
	};
}