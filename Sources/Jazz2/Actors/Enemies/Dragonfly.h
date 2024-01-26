#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	class Dragonfly : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		Dragonfly();
		~Dragonfly();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnHitWall(float timeMult) override;
		void OnHitFloor(float timeMult) override;
		void OnHitCeiling(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr int StateIdle = 0;
		static constexpr int StateAttacking = 1;
		static constexpr int StateBraking = 2;

		int _state;
		float _idleTime;
		float _attackCooldown;
		Vector2f _direction;
		std::shared_ptr<AudioBufferPlayer> _noise;
	};
}