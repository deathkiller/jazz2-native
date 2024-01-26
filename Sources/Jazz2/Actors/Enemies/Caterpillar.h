#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	class Caterpillar : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		Caterpillar();

		static void Preload(const ActorActivationDetails& details);

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;

	private:
		static constexpr int StateIdle = 0;
		static constexpr int StateAttacking = 1;
		static constexpr int StateDisoriented = 2;

		class Smoke : public EnemyBase
		{
			DEATH_RUNTIME_OBJECT(EnemyBase);

		public:
			bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;

		private:
			float _time;
			Vector2f _baseSpeed;
		};

		int _state;
		int _smokesLeft;
		float _attackTime;

		void Disoriented(int count);
	};
}