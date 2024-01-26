#pragma once

#include "BossBase.h"

namespace Jazz2::Actors::Bosses
{
	class Devan : public BossBase
	{
		DEATH_RUNTIME_OBJECT(BossBase);

	public:
		Devan();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnActivatedBoss() override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr int StateTransition = -1;
		static constexpr int StateWaiting = 0;
		static constexpr int StateWarpingIn = 1;
		static constexpr int StateIdling = 2;
		static constexpr int StateRunning1 = 3;
		static constexpr int StateRunning2 = 4;
		static constexpr int StateDemonFlying = 5;
		static constexpr int StateDemonSpewingFireball = 6;
		static constexpr int StateFalling = 7;

		class Bullet : public EnemyBase
		{
		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdateHitbox() override;
			void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;
			bool OnPerish(ActorBase* collider) override;
			void OnHitFloor(float timeMult) override;
			void OnHitWall(float timeMult) override;
			void OnHitCeiling(float timeMult) override;
		};

		class Fireball : public EnemyBase
		{
		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdateHitbox() override;
			void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;
			bool OnPerish(ActorBase* collider) override;
			void OnHitFloor(float timeMult) override;
			void OnHitWall(float timeMult) override;
			void OnHitCeiling(float timeMult) override;

		private:
			float _timeLeft;
		};

		int _state;
		float _stateTime;
		float _attackTime;
		float _anglePhase;
		int _shots;
		bool _isDemon, _isDead;
		Vector2f _lastPos, _targetPos, _lastSpeed;
		uint8_t _endText;

		void FollowNearestPlayer(int newState, float time);
		void FollowNearestPlayerDemon(float timeMult);
		void Shoot();
		void Run();
	};
}