#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	class TurtleTube : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		TurtleTube();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr float WaterDifference = -16.0f;

		bool _onWater;
		float _phase;
	};
}