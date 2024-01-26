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
		static constexpr int StateTransition = -1;
		static constexpr int StateWaiting = 0;
		static constexpr int StateWaiting2 = 1;

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

		int _state;
		float _stateTime;
		uint8_t _theme, _endText;
		Vector2f _originPos;

		void Teleport();
	};
}