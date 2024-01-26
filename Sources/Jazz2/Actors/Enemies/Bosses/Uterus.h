#pragma once

#include "BossBase.h"
#include "../Crab.h"

namespace Jazz2::Actors::Bosses
{
	class Uterus : public BossBase
	{
		DEATH_RUNTIME_OBJECT(BossBase);

	public:
		Uterus();
		~Uterus();

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
		static constexpr int StateOpen = 1;
		static constexpr int StateClosed = 2;

		static constexpr int ShieldPartCount = 6;

		class ShieldPart : public EnemyBase
		{
			friend class Uterus;

		public:
			float Phase;
			float FallTime;

			bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

			void Recover(float phase);

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
			void OnUpdateHitbox() override;
			bool OnPerish(ActorBase* collider) override;
		};

		int _state;
		float _stateTime;
		uint8_t _endText;
		std::shared_ptr<ShieldPart> _shields[ShieldPartCount];
		Vector2f _originPos, _lastPos;
		float _spawnCrabTime;
		float _anglePhase;
		bool _hasShield;

		void FollowNearestPlayer(float timeMult);
	};
}