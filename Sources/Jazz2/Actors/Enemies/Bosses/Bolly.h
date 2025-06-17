#pragma once

#include "BossBase.h"

namespace Jazz2::Actors::Bosses
{
	/** @brief Bolly (boss) */
	class Bolly : public BossBase
	{
		DEATH_RUNTIME_OBJECT(BossBase);

	public:
		Bolly();
		~Bolly();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnActivatedBoss() override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr std::int32_t StateTransition = -1;
		static constexpr std::int32_t StateWaiting = 0;
		static constexpr std::int32_t StateFlying = 1;
		static constexpr std::int32_t StateNewDirection = 2;
		static constexpr std::int32_t StatePrepairingToAttack = 3;
		static constexpr std::int32_t StateAttacking = 4;

		static constexpr std::int32_t NormalChainLength = 5 * 3;
		static constexpr std::int32_t HardChainLength = 6 * 3;
		static constexpr std::int32_t MaxChainLength = HardChainLength;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class BollyPart : public EnemyBase
		{
			friend class Bolly;

		public:
			float Size;

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
		};

		class Rocket : public EnemyBase
		{
			friend class Bolly;

		public:
			bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

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

		std::int32_t _state;
		float _stateTime;
		uint8_t _endText;
		std::shared_ptr<BollyPart> _bottom;
		//std::shared_ptr<BollyPart> _turret;
		std::shared_ptr<BollyPart> _chain[MaxChainLength];
		Vector2f _originPos;
		float _noiseCooldown;
		std::int32_t _rocketsLeft;
		float _chainPhase;

		//void UpdateTurret(float timeMult);
		void FollowNearestPlayer(std::int32_t newState, float time);
		void FireRocket();
	};
}