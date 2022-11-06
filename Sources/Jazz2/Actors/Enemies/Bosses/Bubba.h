#pragma once

#include "BossBase.h"

namespace Jazz2::Actors::Bosses
{
	class Bubba : public BossBase
	{
	public:
		Bubba();

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
		static constexpr int StateJumping = 1;
		static constexpr int StateFalling = 2;
		static constexpr int StateTornado = 3;
		static constexpr int StateDying = 4;

		class Fireball : public EnemyBase
		{
		public:
			bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
			void OnUpdateHitbox() override;
			void OnEmitLights(SmallVectorImpl<LightEmitter>& lights);
			bool OnPerish(ActorBase* collider) override;

		private:
			float _timeLeft;
		};

		int _state;
		float _stateTime;
		uint8_t _endText;

		void FollowNearestPlayer();
		void TornadoToNearestPlayer();
	};
}