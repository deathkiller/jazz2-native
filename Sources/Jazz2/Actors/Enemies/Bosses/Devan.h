#pragma once

#include "BossBase.h"

#include <memory>

namespace Jazz2::Actors::Bosses
{
	/**
		@brief Devan (boss)
		
		Devan Shell, the main antagonist, fought in his humanoid form. He warps in, then runs around the arena
		shooting bullets and crouching to dodge incoming fire. Once reduced to half health he drops his gun and
		transforms into a flying demon that hovers and spits fireballs; depleting his health in this form makes
		him fall and warp out, ending the fight.
	*/
	class Devan : public BossBase
	{
		DEATH_RUNTIME_OBJECT(BossBase);

	public:
		/** @brief Creates a new instance */
		Devan();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnActivatedBoss() override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;
		float GetGravityModifier(float baseGravity, bool isRising) const override;

	private:
		enum class State {
			Transition = -1,
			Waiting = 0,
			WarpingIn,
			Idling,
			Running1,
			Running2,
			Crouch,
			DemonFlying,
			DemonSpewingFireball,
			Falling,
			Jumping,
			Disoriented
		};

		/** @brief Health of both forms together */
		static constexpr std::int32_t TotalHealth = 400;
		/** @brief Frames before it warps in after the activation (71 ticks) */
		static constexpr float StartDelay = 71.0f / TicksPerFrame;
		/** @brief Frames it stands after warping in (141 ticks) */
		static constexpr float IdleTime = 141.0f / TicksPerFrame;
		/** @brief Running speed in pixels per frame (4 pixels per original tick) */
		static constexpr float RunSpeed = 4.0f * TicksPerFrame;
		/** @brief Ticks between decisions while running */
		static constexpr float EvaluationTicks = 16.0f;
		/** @brief Squared distance in pixels within which a player is noticed (256 px) */
		static constexpr float NoticeDistanceSqr = 65536.0f;
		/** @brief Squared distance in pixels within which it shoots (200 px) */
		static constexpr float ShootDistanceSqr = 40000.0f;
		/** @brief Squared distance in pixels from which it ducks when the player has fired (128 px) */
		static constexpr float CrouchDistanceSqr = 16384.0f;
		/** @brief Vertical distance in pixels within which a player is level with it */
		static constexpr float LevelBand = 32.0f;
		/** @brief Frames a player counts as having fired recently (20 ticks) */
		static constexpr float RecentFireFrames = 20.0f / TicksPerFrame;
		/** @brief Most shots in a row */
		static constexpr std::int32_t MaxShots = 10;
		/** @brief Ticks of ducking */
		static constexpr float CrouchTicks = 27.0f;
		/** @brief Ticks of stopping before it shoots, of raising the gun, of aiming and recovering per shot, and of lowering the gun (measured in JJ2+) */
		static constexpr float RunStopTicks = 9.0f;
		static constexpr float ShootStartTicks = 12.0f;
		static constexpr float ShotAimTicks = 6.0f;
		static constexpr float ShotRecoverTicks = 2.0f;
		static constexpr float ShootEndTicks = 12.0f;
		/** @brief Ticks it stands before a jump, measured in the original */
		static constexpr float JumpRunTicks = 13.0f;
		/** @brief Ticks it stands after a landing before it runs again, measured in the original */
		static constexpr float LandingTicks = 21.0f;
		/** @brief Horizontal speed of a jump in pixels per frame (3 pixels per original tick) */
		static constexpr float JumpSpeedX = 3.0f * TicksPerFrame;
		/** @brief Initial vertical speed of a jump in pixels per frame */
		static constexpr float JumpSpeedY = -5.0f * TicksPerFrame;
		/** @brief How far ahead a wall turns it around while running, in pixels */
		static constexpr float WallProbeDistance = 8.0f;
		/** @brief How much of its height above the feet the wall test skips, so steps and slopes don't count, in pixels */
		static constexpr float WallProbeLift = 12.0f;
		/** @brief Gravity of a jump or a fall in pixels per frame squared (0.125 per original tick squared, measured) */
		static constexpr float JumpGravity = 0.125f * TicksPerFrame * TicksPerFrame;
		/** @brief Ticks of a flight of the demon before it spits a fireball */
		static constexpr float DemonFlightTicks = 128.0f;
		/** @brief Frames of a flight of the demon before it spits a fireball */
		static constexpr float DemonFlightTime = DemonFlightTicks / TicksPerFrame;
		/** @brief Ticks it sits disoriented after its defeat */
		static constexpr float DisorientedTicks = 420.0f;
		/** @brief Horizontal distance in pixels where the demon hovers next to its target */
		static constexpr float HoverOffsetX = 64.0f;
		/** @brief Distance in pixels past the player after which the demon turns around */
		static constexpr float TurnDistance = 32.0f;
		/** @brief Squared distance in pixels within which the demon notices a player (640 px) */
		static constexpr float DemonNoticeDistanceSqr = 409600.0f;
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class DisarmedGun : public ActorBase
		{
		public:
			DisarmedGun();

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
			void OnUpdateHitbox() override;

		private:
			float _timeLeft;
		};

		class Bullet : public EnemyBase
		{
		public:
			Bullet();

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
			float _timeLeft = 0.0f;
			void OnUpdateHitbox() override;
			void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;
			bool OnPerish(ActorBase* collider) override;
			void OnHitFloor(float timeMult) override;
			void OnHitWall(float timeMult) override;
			void OnHitCeiling(float timeMult) override;
		};

		class Fireball : public EnemyBase
		{
		public:
			Fireball();

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
			float _timeLeft;
		};
#endif

		State _state;
		float _stateTime;
		float _attackTime;
		std::int32_t _shots;
		bool _isDemon, _isDead;
		Vector2f _lastPos, _originPos;
		std::uint8_t _endText;

		void FollowNearestPlayer(State newState, float time);
		void FollowNearestPlayerDemon(float timeMult);
		void Shoot();
		void Run();
		void Crouch();
		void StartRunning();
		void Jump();
		void Evaluate();
		float _evalAcc;
		float _demonTicks = 0.0f;
		bool _hoverSet = false;
		float _hoverX = 0.0f, _hoverY = 0.0f;
		Vector2f _demonVel;
		std::weak_ptr<ActorBase> _demonTarget;
		float _jumpTicks;
		bool _jumpLaunched = false;
		bool _fellOffLedge = false;

		void Land();
	};
}