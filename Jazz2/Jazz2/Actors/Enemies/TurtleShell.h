#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	class TurtleShell : public EnemyBase
	{
	public:
		TurtleShell();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;
		bool OnHandleCollision(ActorBase* other) override;
		void OnHitFloor() override;

	private:
		float _lastAngle;
	};
}