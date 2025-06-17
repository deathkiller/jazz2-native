#pragma once

#include "BossBase.h"

namespace Jazz2::Actors::Bosses
{
	/** @brief Bubba (boss) */
	class Bubba : public BossBase
	{
		DEATH_RUNTIME_OBJECT(BossBase);

	public:
		Bubba();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnActivatedBoss() override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;
		void OnHitWall(float timeMult) override;

	private:
		enum class State {
			Transition = -1,
			Waiting = 0,
			Jumping = 1,
			Falling = 2,
			Tornado = 3,
			Dying = 4
		};

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class Fireball : public EnemyBase
		{
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
		};
#endif

		State _state;
		float _stateTime;
		float _tornadoCooldown;
		std::uint8_t _endText;
		std::shared_ptr<AudioBufferPlayer> _tornadoNoise;

		void FollowNearestPlayer();
		void TornadoToNearestPlayer();
	};
}