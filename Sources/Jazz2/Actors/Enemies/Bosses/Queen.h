#pragma once

#include "BossBase.h"

namespace Jazz2::Actors::Bosses
{
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
		static constexpr int StateTransition = -1;
		static constexpr int StateWaiting = 0;
		static constexpr int StateIdleToScream = 1;
		static constexpr int StateIdleToStomp = 2;
		static constexpr int StateIdleToBackstep = 3;
		static constexpr int StateDead = 4;
		static constexpr int StateScreaming = 5;

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

		int _state = StateWaiting;
		float _stateTime;
		uint8_t _endText;
		int _lastHealth;
		bool _queuedBackstep;
		float _stepSize;
		Vector2f _originPos;
		std::shared_ptr<InvisibleBlock> _block;
	};
}