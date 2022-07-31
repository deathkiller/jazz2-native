#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	class MadderHatter : public EnemyBase
	{
	public:
		MadderHatter();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr float DefaultSpeed = 0.7f;

		class BulletSpit : public EnemyBase
		{
		public:
			bool OnHandleCollision(ActorBase* other) override;

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdateHitbox() override;
			void OnHitFloor();
			void OnHitWall();
			void OnHitCeiling();
		};

		float _attackTime;
		bool _stuck;

		void Idle(float timeMult);
		void Walking(float timeMult);
		void Attack();
	};
}