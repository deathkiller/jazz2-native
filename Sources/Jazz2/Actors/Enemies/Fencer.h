#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	class Fencer : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		Fencer();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		float _stateTime;

		bool FindNearestPlayer(Vector2f& targetPos);
	};
}