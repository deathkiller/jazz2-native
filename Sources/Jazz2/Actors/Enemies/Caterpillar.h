#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Caterpillar
		
		Stationary, invulnerable creature that inhales and then exhales clouds of smoke; the smoke
		drifts upward and leaves the player dizzy on contact. It cannot be killed, only briefly
		disoriented by shooting it, which interrupts its attack.
	*/
	class Caterpillar : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Caterpillar();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

		bool OnHandleCollision(ActorBase* other) override;

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
			bool OnHandleCollision(ActorBase* other) override;

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