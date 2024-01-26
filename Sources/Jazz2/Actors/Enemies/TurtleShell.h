#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	class TurtleShell : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		TurtleShell();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;
		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;
		void OnHitFloor(float timeMult) override;

	private:
		float _lastAngle;
	};
}