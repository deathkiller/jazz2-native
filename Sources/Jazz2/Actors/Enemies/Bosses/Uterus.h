#pragma once

#include "BossBase.h"
#include "../Crab.h"

namespace Jazz2::Actors::Bosses
{
	/** @brief Uterus (boss) */
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
		static constexpr std::int32_t StateTransition = -1;
		static constexpr std::int32_t StateWaiting = 0;
		static constexpr std::int32_t StateOpen = 1;
		static constexpr std::int32_t StateClosed = 2;

		static constexpr std::int32_t ShieldPartCount = 6;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
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
#endif

		std::int32_t _state;
		float _stateTime;
		std::uint8_t _endText;
		std::shared_ptr<ShieldPart> _shields[ShieldPartCount];
		Vector2f _originPos, _lastPos;
		float _spawnCrabTime;
		float _anglePhase;
		bool _hasShield;

		void FollowNearestPlayer(float timeMult);
	};
}