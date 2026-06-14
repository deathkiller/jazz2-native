#pragma once

#include "../EnemyBase.h"

namespace Jazz2::Actors::Bosses
{
	/**
	 * @brief Robot (boss)
	 *
	 * A giant robot, piloted by Devan in his remote-control battle. It descends by helicopter rotor, then
	 * runs back and forth chasing the player and firing bouncing spike balls in bursts. It is defeated by
	 * depleting its health, shedding sprite debris as it takes damage.
	 */
	class Robot : public Enemies::EnemyBase
	{
		DEATH_RUNTIME_OBJECT(Enemies::EnemyBase);

	public:
		/** @brief Creates a new instance */
		Robot();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

		/** @brief Activates the boss, making it visible and starting its fight */
		void Activate();
		/** @brief Deactivates the boss, hiding it and returning it to the waiting state */
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