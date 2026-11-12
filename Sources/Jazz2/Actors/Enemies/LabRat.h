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
		float GetGravityModifier(float baseGravity, bool isRising) const override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	private:
		static constexpr float DefaultSpeed = TicksPerFrame;
		/** @brief Speed of the leap on both axes at its start, 3 pixels per tick of the original */
		static constexpr float LeapSpeed = 3.0f * TicksPerFrame;
		/** @brief Gravity of the leap in pixels per frame squared, 0.125 per tick squared of the original */
		static constexpr float LeapGravity = 0.125f * TicksPerFrame * TicksPerFrame;
		/** @brief Ticks each frame of the leap animation takes */
		static constexpr float LeapFrameTicks = 7.0f;
		/** @brief Ticks it sits after it lands */
		static constexpr float LandingTicks = 14.0f;

		float _attackTime;
		float _landingTime;
		bool _isAttacking;
		bool _canAttack;
		bool _isWaiting;

		void Walking(float timeMult);
		void SetWaiting(bool value);
		void Attack();
	};
}