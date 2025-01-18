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
		static constexpr std::int32_t StateIdle = 0;
		static constexpr std::int32_t StateAttacking = 1;
		static constexpr std::int32_t StateDisoriented = 2;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
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
#endif

		std::int32_t _state;
		std::int32_t _smokesLeft;
		float _attackTime;

		void Disoriented(std::int32_t count);
	};
}