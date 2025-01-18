#pragma once

#include "BossBase.h"

namespace Jazz2::Actors::Bosses
{
	class Bilsy : public BossBase
	{
		DEATH_RUNTIME_OBJECT(BossBase);

	public:
		Bilsy();

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
			bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

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