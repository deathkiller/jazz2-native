#pragma once

#include "../EnemyBase.h"

namespace Jazz2::Actors
{
	class Player;
}

namespace Jazz2::Actors::Bosses
{
	/**
		@brief Robot (boss)

		A giant robot, piloted by Devan in his remote-control battle. It descends by helicopter rotor, then
		runs toward the nearest player and, when one is close enough, lobs spike balls. It is defeated by
		depleting its health, after which it keeps exploding for a while, shedding sprite debris as it takes damage.
	*/
	class Robot : public Enemies::EnemyBase
	{
		DEATH_RUNTIME_OBJECT(Enemies::EnemyBase);

	public:
		/** @brief Creates a new instance */
		Robot();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

		/** @brief Activates the boss, making it visible and starting its fight */
		void Activate();
		/** @brief Deactivates the boss, hiding it and returning it to the waiting state */
		void Deactivate();

		/** @brief Returns `true` when the explosion sequence after its defeat is over */
		bool IsDeathFinished() const;

	protected:
		// Activated and controlled by DevanRemote, which is never restored, so it has to come back dormant too
		bool IsSerializable() const override { return false; }
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnHealthChanged(ActorBase* collider) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr std::int32_t StateWaiting = 0;
		static constexpr std::int32_t StateCopter = 1;
		static constexpr std::int32_t StateLanding = 2;
		static constexpr std::int32_t StateRunning = 3;
		static constexpr std::int32_t StateIdle = 4;
		static constexpr std::int32_t StateAttacking = 5;
		static constexpr std::int32_t StateDying = 6;
		static constexpr std::int32_t StateDead = 7;

		/** @brief Descent speed in pixels per frame (1 pixel per original tick) */
		static constexpr float DropSpeed = 1.0f * TicksPerFrame;
		/** @brief Ticks of the landing animation */
		static constexpr float LandingTicks = 40.0f;
		/** @brief Ticks of one loop of the descent and run animations, measured in JJ2+ */
		static constexpr float CopterLoopTicks = 20.0f;
		static constexpr float RunLoopTicks = 64.0f;
		/** @brief Ticks of waiting before a decision is made */
		static constexpr std::int32_t DecisionDelayTicks = 35;
		/** @brief Squared distance in pixels within which a player is noticed (400 px) */
		static constexpr float NoticeDistanceSqr = 160000.0f;
		/** @brief Squared distance in pixels within which it attacks (96 px) */
		static constexpr float AttackDistanceSqr = 9216.0f;
		/** @brief Running speed bonus in pixels per tick of the fast gait */
		static constexpr float FastGaitSpeed = 2.0f;
		/** @brief Running speed bonus in pixels per tick of the slow gait */
		static constexpr float SlowGaitSpeed = 0.75f;
		/** @brief Ticks of the start of an attack */
		static constexpr float AttackStartTicks = 45.0f;
		/** @brief Ticks of a single shot */
		static constexpr float ShotTicks = 30.0f;
		/** @brief Tick of a shot at which the spike ball is thrown */
		static constexpr float ShotThrowTick = 15.0f;
		/** @brief Ticks of the end of an attack */
		static constexpr float AttackEndTicks = 45.0f;
		/** @brief Ticks of the explosion sequence after its defeat */
		static constexpr float DyingTicks = 280.0f;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class SpikeBall : public Enemies::EnemyBase
		{
		public:
			/** @brief Sets the horizontal launch speed in pixels per original tick */
			void SetLaunchSpeed(float speedPerTick) { _launchSpeed = speedPerTick; }

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
			void OnUpdateHitbox() override;
			void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;
			bool OnPerish(ActorBase* collider) override;
			void OnHitFloor(float timeMult) override;
			void OnHitWall(float timeMult) override;
			void OnHitCeiling(float timeMult) override;

		private:
			/** @brief Lifetime in frames (35 ticks) */
			static constexpr float Lifetime = 35.0f / TicksPerFrame;
			/** @brief Gravity in pixels per frame squared (1/16 per original tick squared) */
			static constexpr float Gravity = 0.0625f * TicksPerFrame * TicksPerFrame;

			float _launchSpeed = 2.0f;
			float _timeLeft = Lifetime;
		};
#endif

		std::int32_t _state;
		float _ticks;
		float _gaitPhase;
		float _gaitBase;
		float _tickAcc;
		std::int32_t _counter;
		std::int32_t _shots;
		std::int32_t _attackPhase;
		bool _ballThrown;
		Vector2f _originPos;
		float _ballSpeed;
		float _deathAcc;

		bool HasGroundBelow() const;
		void StartRunning();
		void StartIdle();
		void StartAttack(float distanceSqr);
		void StartShot();
		void EndShot();
		void LogicTick();
		void UpdateAttack(float timeMult);
		void UpdateDying(float timeMult);
		void ThrowSpikeBall();
	};
}
