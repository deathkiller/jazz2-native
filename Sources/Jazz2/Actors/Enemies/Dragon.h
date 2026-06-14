#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
	 * @brief Dragon
	 *
	 * Small dragon that stays in place and faces the nearest player within range, then spits a stream
	 * of short-lived fireballs at them before pausing between bursts. Defeated by a single hit.
	 */
	class Dragon : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Dragon();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr float DefaultSpeed = 0.7f;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class Fire : public EnemyBase
		{
			DEATH_RUNTIME_OBJECT(EnemyBase);

		protected:
			Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
			void OnUpdate(float timeMult) override;
			void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;

		private:
			float _timeLeft;
		};
#endif

		bool _attacking;
		float _stateTime;
		float _attackTime;
	};
}