#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Dragonfly
		
		Flying insect that hovers and drifts erratically, then periodically buzzes and darts in a
		straight line toward a nearby player before braking to a stop, staying above the water surface.
		Defeated by a single hit.
	*/
	class Dragonfly : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Dragonfly();
		~Dragonfly();

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

		std::int32_t _state;
		float _idleTime;
		float _attackCooldown;
		Vector2f _direction;
		std::shared_ptr<AudioBufferPlayer> _noise;
	};
}