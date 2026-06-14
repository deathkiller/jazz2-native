#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Fish
		
		Underwater enemy that drifts idly while submerged and, when a player swims within range, darts at
		them in a quick lunge before braking and returning to its starting spot. Stays below the water
		surface and is defeated by a single hit.
	*/
	class Fish : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Fish();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnHitWall(float timeMult) override;
		void OnHitFloor(float timeMult) override;
		void OnHitCeiling(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr std::int32_t StateIdle = 0;
		static constexpr std::int32_t StateAttacking = 1;
		static constexpr std::int32_t StateBraking = 2;
		static constexpr std::int32_t StateReturning = 3;

		std::int32_t _state;
		float _idleTime;
		float _attackCooldown;
		Vector2f _direction;
		Vector2f _originPos;
	};
}