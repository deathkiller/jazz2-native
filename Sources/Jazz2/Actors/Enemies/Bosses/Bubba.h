#pragma once

#include "BossBase.h"

namespace Jazz2::Actors::Bosses
{
	/**
	 * @brief Bubba (boss)
	 *
	 * A large frog boss that bounds around the arena in big jumps toward the nearest player. On landing it
	 * randomly either spews a fireball, whirls into a tornado dash, or jumps again. It is defeated by
	 * depleting its health between attacks, after which it collapses in a death animation.
	 */
	class Bubba : public BossBase
	{
		DEATH_RUNTIME_OBJECT(BossBase);

	public:
		/** @brief Creates a new instance */
		Bubba();

		/** @brief Preloads all assets required by this actor */
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
			bool OnHandleCollision(ActorBase* other) override;

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