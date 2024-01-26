#pragma once

#include "../EnemyBase.h"

namespace Jazz2::Actors::Bosses
{
	class Robot : public Enemies::EnemyBase
	{
		DEATH_RUNTIME_OBJECT(Enemies::EnemyBase);

	public:
		Robot();

		static void Preload(const ActorActivationDetails& details);

		void Activate();
		void Deactivate();

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnHealthChanged(ActorBase* collider) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr int StateTransition = -1;
		static constexpr int StateWaiting = 0;
		static constexpr int StateCopter = 1;
		static constexpr int StateRunning1 = 2;
		static constexpr int StateRunning2 = 3;
		static constexpr int StatePreparingToRun = 4;

		class SpikeBall : public Enemies::EnemyBase
		{
		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdateHitbox() override;
			void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;
			bool OnPerish(ActorBase* collider) override;
			void OnHitFloor(float timeMult) override;
			void OnHitWall(float timeMult) override;
			void OnHitCeiling(float timeMult) override;
		};

		int _state;
		float _stateTime;
		int _shots;
		Vector2f _originPos;

		void FollowNearestPlayer(int newState, float time);
		void Shoot();
		void Run();
	};
}