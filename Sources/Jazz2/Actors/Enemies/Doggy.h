#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Doggy
		
		Dog that trots back and forth along the ground, turning at walls and ledges. When a player touches
		or shoots it, it turns to them and runs barking, fast while the player is ahead of it, until it
		has to turn or tires out. Appears as a cat in the Secret Files episode. Takes several hits to defeat.
	*/
	class Doggy : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Doggy();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

		bool OnHandleCollision(ActorBase* other) override;

		/** @brief Returns `true` while it runs after a player that touched or shot it */
		bool IsAttacking() const {
			return (_attackTime > 0.0f);
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		float GetTurnLookAhead() const override {
			return 34.0f;
		}
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	private:
		float _attackTime;
		float _noiseCooldown;
		std::weak_ptr<ActorBase> _attackTarget;
		bool _stuck;

		void StartAttack(Player* player);
		void EndAttack();
		bool CanAttackRunOn();
		void UpdateSpeed();
		void ResetSniffTime();
	};
}