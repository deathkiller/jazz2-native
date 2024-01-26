#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	class Skeleton : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		Skeleton();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		void OnHealthChanged(ActorBase* collider) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr float DefaultSpeed = 0.7f;

		bool _stuck;
	};
}