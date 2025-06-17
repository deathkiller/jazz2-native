#pragma once

#include "BossBase.h"

namespace Jazz2::Actors::Bosses
{
	/** @brief Queen (boss) */
	class Queen : public BossBase
	{
		DEATH_RUNTIME_OBJECT(BossBase);

	public:
		Queen();
		~Queen();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnActivatedBoss() override;
		void OnUpdate(float timeMult) override;

	private:
		static constexpr std::int32_t StateTransition = -1;
		static constexpr std::int32_t StateWaiting = 0;
		static constexpr std::int32_t StateIdleToScream = 1;
		static constexpr std::int32_t StateIdleToStomp = 2;
		static constexpr std::int32_t StateIdleToBackstep = 3;
		static constexpr std::int32_t StateDead = 4;
		static constexpr std::int32_t StateScreaming = 5;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class Brick : public EnemyBase
		{
		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
			bool OnPerish(ActorBase* collider) override;

		private:
			float _timeLeft;
		};

		class InvisibleBlock : public ActorBase
		{
			friend class Queen;

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
		};
#endif

		std::int32_t _state = StateWaiting;
		float _stateTime;
		std::uint8_t _endText;
		std::int32_t _lastHealth;
		bool _queuedBackstep;
		float _stepSize;
		Vector2f _originPos;
		std::shared_ptr<InvisibleBlock> _block;
	};
}