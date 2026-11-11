#pragma once

#include "BossBase.h"
#include "../Crab.h"

namespace Jazz2::Actors::Bosses
{
	/**
		@brief Uterus (boss)

		A floating brain-like boss that drifts around the arena, encircled by a ring of orbiting shield parts.
		Shooting it while shielded makes it drop a Crab, shooting the shield parts three times destroys them.
		Once all of them are gone it can be hurt while it is open, and it is defeated by depleting its health.
	*/
	class Uterus : public BossBase
	{
		DEATH_RUNTIME_OBJECT(BossBase);

	public:
		/** @brief Creates a new instance */
		Uterus();
		~Uterus();

		bool OnHandleCollision(ActorBase* other) override;

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnActivatedBoss() override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr std::int32_t StateWaiting = 0;
		static constexpr std::int32_t StateActive = 1;

		/** @brief Most shield parts, there are 4 on Easy, 6 on Normal and 8 on Hard */
		static constexpr std::int32_t MaxShieldPartCount = 8;
		/** @brief Ticks of the whole cycle of opening and closing */
		static constexpr float CycleTicks = 512.0f;
		/** @brief Tick of the cycle at which it starts to open */
		static constexpr float OpenStartTick = 128.0f;
		/** @brief Tick of the cycle from which it can be hurt */
		static constexpr float VulnerableStartTick = 160.0f;
		/** @brief Tick of the cycle at which it starts to close */
		static constexpr float CloseStartTick = 448.0f;
		/** @brief Tick of the cycle from which it cannot be hurt */
		static constexpr float VulnerableEndTick = 488.0f;
		/** @brief Ticks from a shot at a shielded Uterus until the Crab is gone and the next can come */
		static constexpr float CrabTicks = 140.0f;
		/** @brief Ticks from a shot at a shielded Uterus until the Crab drops */
		static constexpr float CrabDropTick = 70.0f;
		/** @brief Radius in pixels of the orbit of the shield parts */
		static constexpr float ShieldRadius = 64.0f;
		/** @brief Horizontal and vertical size in pixels of the motion of the body */
		static constexpr float WobbleX = 64.0f;
		static constexpr float WobbleY = 32.0f;
		/** @brief Distance in pixels above the player which it drifts to while shielded */
		static constexpr float HoverHeight = 100.0f;
		/** @brief Squared distance in pixels within which a player is followed (400 px) */
		static constexpr float FollowDistanceSqr = 160000.0f;
		/** @brief Angular speed of the orbit in units of 1/1024 turn per tick, shielded and exposed */
		static constexpr float ShieldedOrbitSpeed = 2.0f;
		static constexpr float ExposedOrbitSpeed = 8.0f;
		/** @brief Ticks a knocked off shield part falls for */
		static constexpr float ShieldFallTicks = 140.0f;
		/** @brief Ticks a shield part needs to fly back */
		static constexpr float ShieldReturnTicks = 64.0f;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class ShieldPart : public EnemyBase
		{
			friend class Uterus;

		public:
			float Phase;
			float FallTime;
			float ReturnTime;

			bool OnHandleCollision(ActorBase* other) override;

			void Recover(float phase);

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
			void OnUpdateHitbox() override;
			bool OnPerish(ActorBase* collider) override;
			float GetGravityModifier(float baseGravity, bool isRising) const override;

		private:
			/** @brief Gravity of a knocked off part in pixels per frame squared (0.125 per original tick squared) */
			static constexpr float FallGravity = 0.125f * TicksPerFrame * TicksPerFrame;
		};
#endif

		std::int32_t _state;
		std::uint8_t _endText;
		std::int32_t _shieldCount;
		std::shared_ptr<ShieldPart> _shields[MaxShieldPartCount];
		Vector2f _originPos, _lastPos;
		float _phase;
		float _angle;
		float _crabTicks;
		bool _hasShield;
		bool _touchable;

		void FollowNearestPlayer(float timeMult);
		void UpdateVulnerability();
		void SpawnCrab();
	};
}
