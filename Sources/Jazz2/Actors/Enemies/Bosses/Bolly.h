#pragma once

#include "BossBase.h"

namespace Jazz2::Actors::Bosses
{
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
		static constexpr int StateTransition = -1;
		static constexpr int StateWaiting = 0;
		static constexpr int StateFlying = 1;
		static constexpr int StateNewDirection = 2;
		static constexpr int StatePrepairingToAttack = 3;
		static constexpr int StateAttacking = 4;

		static constexpr int NormalChainLength = 5 * 3;
		static constexpr int HardChainLength = 6 * 3;
		static constexpr int MaxChainLength = HardChainLength;

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

		int _state;
		float _stateTime;
		uint8_t _endText;
		std::shared_ptr<BollyPart> _bottom;
		//std::shared_ptr<BollyPart> _turret;
		std::shared_ptr<BollyPart> _chain[MaxChainLength];
		Vector2f _originPos;
		float _noiseCooldown;
		int _rocketsLeft;
		float _chainPhase;

		//void UpdateTurret(float timeMult);
		void FollowNearestPlayer(int newState, float time);
		void FireRocket();
	};
}