#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/** @brief Madder hatter */
	class MadderHatter : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

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

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class BulletSpit : public EnemyBase
		{
			DEATH_RUNTIME_OBJECT(EnemyBase);

		public:
			bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
			void OnUpdateHitbox() override;
			bool OnPerish(ActorBase* collider) override;
			void OnHitFloor(float timeMult) override;
			void OnHitWall(float timeMult) override;
			void OnHitCeiling(float timeMult) override;

		private:
			float _timeLeft;
		};
#endif

		float _attackTime;
		bool _stuck;

		void Idle(float timeMult);
		void Walking(float timeMult);
		void Attack();
	};
}