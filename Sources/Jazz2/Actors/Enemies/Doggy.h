#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	class Doggy : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		Doggy();

		static void Preload(const ActorActivationDetails& details);

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		float _attackSpeed;
		float _attackTime;
		float _noiseCooldown;
		bool _stuck;
	};
}