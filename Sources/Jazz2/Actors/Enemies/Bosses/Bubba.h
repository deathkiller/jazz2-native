#pragma once

#include "BossBase.h"

#include <memory>

namespace Jazz2::Actors::Bosses
{
	/**
		@brief Bubba (boss)

		A large frog boss that bounds around the arena in big jumps. When it lands it jumps again, spews a
		fireball at a player standing level with it, or whirls into a tornado dash at the spot where a player
		stood. It is defeated by depleting its health.
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
		Vector2f GetContactThrowSpeed() const override;

	private:
		enum class State {
			Waiting,
			Crouching,
			Rising,
			Falling,
			Landing,
			Spewing,
			TornadoWindUp,
			TornadoMoving,
			TornadoStopping
		};

		/** @brief Health on Easy, Normal and Hard */
		static constexpr std::int32_t HealthByDifficulty[3] = { 60, 92, 124 };
		/** @brief Squared distance in pixels within which a player is noticed (447 px) */
		static constexpr float NoticeDistanceSqr = 200000.0f;
		/** @brief Vertical distance in pixels within which a player counts as level with the boss */
		static constexpr float LevelBand = 32.0f;
		/** @brief Horizontal distance in pixels from which a player is too far for a fireball */
		static constexpr float FireballRange = 192.0f;
		/** @brief Horizontal speed of a jump in pixels per frame (1 pixel per original tick) */
		static constexpr float JumpSpeedX = 1.0f * TicksPerFrame;
		/** @brief Initial vertical speed of a jump in pixels per frame */
		static constexpr float JumpSpeedY = -5.0f * TicksPerFrame;
		/** @brief Gravity in pixels per frame squared (0.125 per original tick squared) */
		static constexpr float Gravity = 0.125f * TicksPerFrame * TicksPerFrame;
		/** @brief Maximum falling speed in pixels per frame */
		static constexpr float MaxFallSpeed = 6.0f * TicksPerFrame;
		/** @brief Ticks of crouching before a jump */
		static constexpr float CrouchTicks = 12.0f;
		/** @brief Ticks of recovery after landing */
		static constexpr float LandingTicks = 20.0f;
		/** @brief Ticks of the fireball animation */
		static constexpr float SpewTicks = 64.0f;
		/** @brief Tick of the fireball animation at which the fireball is spawned */
		static constexpr float SpewFireballTick = 44.0f;
		/** @brief Ticks of the start of the tornado */
		static constexpr float TornadoWindUpTicks = 36.0f;
		/** @brief Ticks of the end of the tornado */
		static constexpr float TornadoStopTicks = 23.0f;
		/** @brief Horizontal distance in pixels from the home position after which it turns back */
		static constexpr float HomeRange = 128.0f;
		/** @brief Distance in pixels past a player after which it turns back */
		static constexpr float PastPlayerRange = 32.0f;

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
			/** @brief Lifetime in frames (70 ticks) */
			static constexpr float Lifetime = 70.0f / TicksPerFrame;

			float _timeLeft;
		};
#endif

		State _state;
		float _ticks;
		Vector2f _vel;
		Vector2f _homePos;
		Vector2f _tornadoTarget;
		float _tornadoSpeed;
		float _tornadoMoveTicks;
		bool _fireballSpawned;
		std::uint8_t _endText;
		std::weak_ptr<ActorBase> _target;
		std::shared_ptr<AudioBufferPlayer> _tornadoNoise;

		void Decide();
		void StartJump();
		void StartSpew();
		void StartTornado(Player* target);
		bool HasGroundBelow(float dy) const;
		void Land();
		void StopTornadoNoise();
	};
}
