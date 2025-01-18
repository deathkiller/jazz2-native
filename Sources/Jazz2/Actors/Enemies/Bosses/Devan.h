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
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		enum class State {
			Transition = -1,
			Waiting = 0,
			WarpingIn,
			Idling,
			Running1,
			Running2,
			Crouch,
			DemonFlying,
			DemonSpewingFireball,
			Falling
		};

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class DisarmedGun : public ActorBase
		{
		public:
			DisarmedGun();

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
			void OnUpdateHitbox() override;

		private:
			float _timeLeft;
		};

		class Bullet : public EnemyBase
		{
		public:
			Bullet();

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
		public:
			Fireball();

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
#endif

		State _state;
		float _stateTime;
		float _attackTime;
		float _anglePhase;
		float _crouchCooldown;
		std::int32_t _shots;
		bool _isDemon, _isDead;
		Vector2f _lastPos, _targetPos, _lastSpeed;
		std::uint8_t _endText;

		void FollowNearestPlayer(State newState, float time);
		void FollowNearestPlayerDemon(float timeMult);
		void Shoot();
		void Run();
		void Crouch();
		bool ShouldCrouch() const;
	};
}