#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	class Dragon : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		Dragon();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr float DefaultSpeed = 0.7f;

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

		bool _attacking;
		float _stateTime;
		float _attackTime;
	};
}