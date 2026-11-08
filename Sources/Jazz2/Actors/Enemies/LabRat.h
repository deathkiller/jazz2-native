#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Lab rat
		
		Rat that scurries back and forth along the ground, squeaking, and turns around at walls and ledges. When a player comes near in front of it, it pounces with a
		short hopping lunge. Defeated by a single hit.
	*/
	class LabRat : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		LabRat();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		float GetTurnLookAhead() const override {
			return 41.0f;
		}
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	private:
		static constexpr float DefaultSpeed = TicksPerFrame;

		float _attackTime;
		float _jumpPushTime;
		bool _isAttacking;
		bool _canAttack;

		void Walking(float timeMult);
		void Attack();
	};
}