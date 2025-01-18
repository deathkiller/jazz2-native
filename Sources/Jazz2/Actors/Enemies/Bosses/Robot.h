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
		static constexpr std::int32_t StateTransition = -1;
		static constexpr std::int32_t StateWaiting = 0;
		static constexpr std::int32_t StateCopter = 1;
		static constexpr std::int32_t StateRunning1 = 2;
		static constexpr std::int32_t StateRunning2 = 3;
		static constexpr std::int32_t StatePreparingToRun = 4;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
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
#endif

		std::int32_t _state;
		float _stateTime;
		std::int32_t _shots;
		Vector2f _originPos;

		void FollowNearestPlayer(std::int32_t newState, float time);
		void Shoot();
		void Run();
	};
}