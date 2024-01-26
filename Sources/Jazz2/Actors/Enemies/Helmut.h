#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	class Helmut : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		Helmut();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr float DefaultSpeed = 0.9f;

		bool _idling;
		float _stateTime;
		bool _stuck;
		float _turnCooldown;
	};
}