#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	class Lizard : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		Lizard();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		void OnHitFloor(float timeMult) override;
		void OnHitWall(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr float DefaultSpeed = 1.0f;

		bool _stuck;
		bool _isFalling;
	};
}