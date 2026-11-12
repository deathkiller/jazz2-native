#pragma once

#include "BossBase.h"

#include <memory>

namespace Jazz2::Actors
{
	class Player;
}

namespace Jazz2::Actors::Bosses
{
	/**
		@brief Bilsy (boss)

		The fire-throwing demon summoned by Devan. He appears near his home spot, and when a player is close he
		either throws a homing fireball (when facing the player) or vanishes, then reappears a few steps to the
		side. He is defeated once his health is depleted.
	*/
	class Bilsy : public BossBase
	{
		DEATH_RUNTIME_OBJECT(BossBase);

	public:
		/** @brief Creates a new instance */
		Bilsy();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

		bool TakesSingleBlastDamage() const override {
			return true;
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnActivatedBoss() override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr std::int32_t StateDormant = 0;
		static constexpr std::int32_t StateStartWait = 1;
		static constexpr std::int32_t StateAppear = 2;
		static constexpr std::int32_t StateIdle = 3;
		static constexpr std::int32_t StateThrow = 4;
		static constexpr std::int32_t StateVanish = 5;

		/** @brief How far below its place the floor is looked for, in pixels */
		static constexpr std::int32_t GroundSearchDistance = 128;
		/** @brief Ticks of the appear animation */
		static constexpr float AppearTicks = 60.0f;
		/** @brief Ticks of the vanish animation */
		static constexpr float VanishTicks = 51.0f;
		/** @brief Idle time after the activation before the appear animation, in frames (71 ticks) */
		static constexpr float StartDelay = 71.0f / TicksPerFrame;
		/** @brief Duration of the appear animation, in frames (20 animation frames of 3 ticks) */
		static constexpr float AppearTime = 60.0f / TicksPerFrame;
		/** @brief Duration of the vanish animation, in frames (17 animation frames of 3 ticks) */
		static constexpr float VanishTime = 51.0f / TicksPerFrame;
		/** @brief Minimum idle time before it acts, in frames (32 ticks) */
		static constexpr float IdleTime = 32.0f / TicksPerFrame;
		/** @brief Time from the start of the throw animation to the fireball, in frames (42 ticks) */
		static constexpr float ThrowFireballTime = 42.0f / TicksPerFrame;
		/** @brief Duration of the throw animation, in frames (54 ticks) */
		static constexpr float ThrowTime = 54.0f / TicksPerFrame;
		/** @brief Squared distance in pixels within which a player is noticed (362 px) */
		static constexpr float NoticeDistanceSqr = 131072.0f;
		/** @brief Horizontal teleport step in pixels, the offset is -15 to +16 steps from the home position */
		static constexpr float TeleportStep = 10.0f;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class Fireball : public EnemyBase
		{
			DEATH_RUNTIME_OBJECT(EnemyBase);

		public:
			/** @brief Sets the player the fireball homes in on */
			void SetTarget(const std::shared_ptr<ActorBase>& target) { _target = target; }

			bool OnHandleCollision(ActorBase* other) override;

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
			void OnUpdateHitbox() override;
			void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;
			bool OnPerish(ActorBase* collider) override;

		private:
			/** @brief Lifetime in frames (141 ticks) */
			static constexpr float Lifetime = 141.0f / TicksPerFrame;
			/** @brief Initial speed value, the speed in pixels per tick is the value divided by 8 */
			static constexpr float InitialSpeedValue = 16.0f;
			/** @brief Lowest speed value */
			static constexpr float MinSpeedValue = 8.0f;

			float _timeLeft;
			float _heading;
			float _speedValue;
			std::weak_ptr<ActorBase> _target;

			void FollowTarget(float timeMult);
		};
#endif

		std::int32_t _state;
		float _stateTime;
		bool _fireballThrown;
		std::uint8_t _theme, _endText;
		Vector2f _originPos;
		std::weak_ptr<ActorBase> _target;

		Player* FindTarget() const;
		void Teleport();
	};
}
