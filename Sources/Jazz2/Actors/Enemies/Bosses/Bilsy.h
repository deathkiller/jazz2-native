#pragma once

#include "BossBase.h"

namespace Jazz2::Actors::Bosses
{
	/**
		@brief Bilsy (boss)
		
		The fire-throwing demon summoned by Devan. He repeatedly teleports to random spots around the arena,
		then turns to face the nearest player and spews a homing fireball before vanishing again. He is only
		vulnerable while materialized between teleports and is defeated once his health is depleted.
	*/
	class Bilsy : public BossBase
	{
		DEATH_RUNTIME_OBJECT(BossBase);

	public:
		/** @brief Creates a new instance */
		Bilsy();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnActivatedBoss() override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr std::int32_t StateTransition = -1;
		static constexpr std::int32_t StateWaiting = 0;
		static constexpr std::int32_t StateWaiting2 = 1;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class Fireball : public EnemyBase
		{
			DEATH_RUNTIME_OBJECT(EnemyBase);

		public:
			bool OnHandleCollision(ActorBase* other) override;

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
			void OnUpdateHitbox() override;
			void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;
			bool OnPerish(ActorBase* collider) override;

		private:
			float _timeLeft;

			void FollowNearestPlayer();
		};
#endif

		std::int32_t _state;
		float _stateTime;
		std::uint8_t _theme, _endText;
		Vector2f _originPos;

		void Teleport();
	};
}