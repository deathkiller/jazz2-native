#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	class Crab : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		Crab();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr float DefaultSpeed = 0.7f;

		float _noiseCooldown;
		float _stepCooldown;
		bool _canJumpPrev;
		bool _stuck;
	};
}